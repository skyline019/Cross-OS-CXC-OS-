#!/usr/bin/env python3
"""Bounded model of a proposed direct-PMEM window allocator, not kernel code.

Groups are 64-line software allocation summaries, not device blocks. A caller
must derive free masks from both protected roots, in-flight reservations and
readers under the allocator lock. The DP only optimizes a small candidate set;
if it cannot satisfy the request, a complete first-fit scan prevents the cap
from inventing ENOSPC. The cost is a software proxy, not measured time.
"""

from dataclasses import dataclass
from itertools import product


@dataclass(frozen=True)
class Span:
    group: int
    start: int
    length: int


@dataclass(frozen=True)
class Plan:
    mode: str
    cost: int
    # Each selected tuple is (group, first line within group, line count).
    extents: tuple


def free_masks(width, active, previous, reservations, readers):
    """Return free bits after *all* protections, for equal-sized groups."""
    arrays = (active, previous, reservations, readers)
    if not 1 <= width <= 64 or not active or len(active) > 4:
        raise ValueError("one to four groups of 1..64 lines required")
    if any(len(a) != len(active) for a in arrays):
        raise ValueError("protection arrays must have the same group count")
    valid = (1 << width) - 1
    if any(mask < 0 or mask & ~valid for a in arrays for mask in a):
        raise ValueError("mask outside group")
    return tuple(valid & ~(a | b | c | d) for a, b, c, d in zip(*arrays))


def enumerate_spans(masks, width):
    if not 1 <= width <= 64 or not masks or len(masks) > 4:
        raise ValueError("one to four groups of 1..64 lines required")
    valid = (1 << width) - 1
    spans = []
    for group, mask in enumerate(masks):
        if mask < 0 or mask & ~valid:
            raise ValueError("mask outside group")
        i = 0
        while i < width:
            if not (mask & (1 << i)):
                i += 1
                continue
            start = i
            while i < width and (mask & (1 << i)):
                i += 1
            spans.append(Span(group, start, i - start))
    return tuple(spans)


def span_cost(span, take):
    # Opening a physical run costs 2; leaving a suffix costs 1. This objective
    # is intentionally simple and must be validated against actual workloads.
    return 0 if take == 0 else 2 + int(take < span.length)


def plan_window(masks, width, count, span_cap=16):
    """Choose at most 64 lines; return None only after a complete free scan.

    For each candidate maximal free run, choose either no lines or a prefix.
    Dynamic programming minimizes the defined cost over the first span_cap
    runs. If that subset has insufficient capacity, a safe first-fit fallback
    examines every run. Caller must reserve under the same allocator lock, or
    revalidate all bits after reacquiring it.
    """
    spans = enumerate_spans(masks, width)
    if not 0 <= count <= 64 or not 1 <= span_cap <= 16:
        raise ValueError("count must be 0..64 and span_cap must be 1..16")
    # dp[k] = (cost, tuple of chosen extents), for the processed runs.
    dp = [None] * (count + 1)
    dp[0] = (0, ())
    for span in spans[:span_cap]:
        next_dp = [None] * (count + 1)
        for used, item in enumerate(dp):
            if item is None:
                continue
            for take in range(min(span.length, count - used) + 1):
                extents = item[1] + (((span.group, span.start, take),) if take else ())
                candidate = (item[0] + span_cost(span, take), extents)
                k = used + take
                if next_dp[k] is None or candidate < next_dp[k]:
                    next_dp[k] = candidate
        dp = next_dp
    if dp[count] is not None:
        return Plan("dp", dp[count][0], dp[count][1])

    # The capped subset was insufficient; scan every remaining span. Do not
    # release protection or consider pending-zero-reference objects free.
    left, cost, extents = count, 0, []
    for span in spans:
        take = min(left, span.length)
        if take:
            extents.append((span.group, span.start, take))
            cost += span_cost(span, take)
            left -= take
        if not left:
            return Plan("fallback", cost, tuple(extents))
    return None


def allocated_masks(plan, groups, width):
    result = [0] * groups
    for group, start, count in plan.extents:
        if not 0 <= group < groups or count < 1 or start < 0 or start + count > width:
            raise AssertionError("out-of-range extent")
        bits = ((1 << count) - 1) << start
        if result[group] & bits:
            raise AssertionError("overlapping extents")
        result[group] |= bits
    return tuple(result)


def brute_cost(spans, count):
    best = None
    for takes in product(*(range(span.length + 1) for span in spans)):
        if sum(takes) == count:
            cost = sum(span_cost(span, n) for span, n in zip(spans, takes))
            best = cost if best is None else min(best, cost)
    return best


def main():
    cases = 0
    # Every free/occupied pattern in two four-line groups and every possible
    # request size. The candidate cap covers all runs here, so brute force is
    # an independent optimum oracle for the bounded cost, not for I/O latency.
    for left in range(16):
        for right in range(16):
            masks = (left, right)
            spans = enumerate_spans(masks, 4)
            for count in range(9):
                result = plan_window(masks, 4, count)
                expected = brute_cost(spans, count)
                if expected is None:
                    assert result is None
                else:
                    assert result is not None and result.mode == "dp"
                    assert result.cost == expected
                    allocated = allocated_masks(result, 2, 4)
                    assert all((allocated[i] & ~masks[i]) == 0 for i in range(2))
                    assert sum(mask.bit_count() for mask in allocated) == count
                cases += 1

    # Capped candidates cannot create a false ENOSPC if later free runs exist.
    result = plan_window((0b0101, 0b1111), 4, 4, span_cap=1)
    assert result is not None and result.mode == "fallback"
    assert sum(mask.bit_count() for mask in allocated_masks(result, 2, 4)) == 4
    assert plan_window((0b0101, 0b1111), 4, 7, span_cap=1) is None
    for invalid_cap in (0, 17):
        try:
            plan_window((0b1111,), 4, 1, span_cap=invalid_cap)
        except ValueError:
            pass
        else:
            raise AssertionError("candidate cap must be bounded")

    # Example where first-fit 5+1 is worse under the stated objective than 3+3.
    example = plan_window((0b11111, 0b001110111), 9, 6)
    assert example is not None and example.cost == 4 and len(example.extents) == 2

    # Three negative controls: forgetting an old-root, in-flight or reader
    # protection may hand out a line which the complete mask protects.
    active, old, inflight, readers = (1,), (2,), (4,), (8,)
    safe = free_masks(8, active, old, inflight, readers)
    assert plan_window(safe, 8, 5) is None
    for omitted in range(3):
        protections = [old, inflight, readers]
        protections[omitted] = (0,)
        unsafe = free_masks(8, active, *protections)
        chosen = allocated_masks(plan_window(unsafe, 8, 5), 1, 8)[0]
        assert chosen & (old, inflight, readers)[omitted][0]

    print(f"WINDOW_ALLOCATOR_MODEL_PASS cases={cases} capped_fallback=1 "
          "negative_controls=3 (bounded proxy; no kernel or QEMU execution)")


if __name__ == "__main__":
    main()
