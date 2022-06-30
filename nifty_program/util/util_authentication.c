#pragma once

#include "inc/program_config.h"
#include "util/util_accounts.c"


// Some instructions can only be invoked if the transaction was signed by the super user.
static bool is_superuser_authenticated(const SolAccountInfo *superuser_account)
{
    // Ensure that the superuser signed this transaction
    return is_superuser_pubkey(superuser_account->key) && superuser_account->is_signer;
}


// Some instructions can only be invoked if the transaction was signed by the admin user.  The admin user is specified
// in the program config account.  For all instructions which require admin privileges, the first account of the
// instruction must be the program config account, and the second account identifies the admin as a signer of the
// transaction.
static bool is_admin_authenticated(const SolAccountInfo *config_account,
                                   const SolAccountInfo *supplied_admin_account)
{
    // Must be the admin account
    if (!is_admin_account(config_account, supplied_admin_account->key)) {
        return false;
    }

    // The admin signer account must be a signer, and not be executable, just because it doesn't need to be
    // executable.  It may or may not be writable, because it may validly be used as a source of funds for the
    // transaction too.
    if (!supplied_admin_account->is_signer || supplied_admin_account->executable) {
        return false;
    }

    // All checks passed.  This transaction was signed by the admin account.
    return true;
}
