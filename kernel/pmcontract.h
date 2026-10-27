// Unified ONBOX_PMFS_* serial contract helpers.
#ifndef XV6_PMCONTRACT_H
#define XV6_PMCONTRACT_H

void pmcontract_pass(char *stage, char *fields);
void pmcontract_fail(char *stage, int code);
void pmcontract_gate(void);  // prints ONBOX_PMFS_PASS if all required ok

void pmcontract_mark(char *stage, int ok);  // record for gate

#endif
