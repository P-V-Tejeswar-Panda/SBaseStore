#ifndef __DATA_DEFS_H__

#include <sys/types.h>

#endif
#define USERID_LENGTH  48
#define PASSWD_LENGTH  16

typedef u_int64_t amount_t;
typedef enum{
    CREATE      = 1,
    DEPOSIT     = 2,
    WITHDRAW    = 4,
    STOP        = 8,
    DELETE      = 16,
    UNKNOWN     = 32,
    SHOWDB      = 64,
} request_t;

typedef struct{
    request_t    req;
    char         userid[USERID_LENGTH];
    union {
        char     passwd[PASSWD_LENGTH];
        amount_t amount;
    };
} request_data_t;

typedef struct{
    char user_id[USERID_LENGTH];
    char passwd[PASSWD_LENGTH];
    amount_t  balance;
} db_entry_t;