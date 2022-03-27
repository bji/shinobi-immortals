// Some instructions can only be invoked if the transaction was signed by the admin user.  The admin user is specified
// in the program config account.  For all instructions which require admin privileges, the first account of the
// instruction must be the program config account, and the second account identifies the admin as a signer of the
// transaction.
static bool is_admin_authenticated(const SolAccountInfo *config_account,
                                   const SolAccountInfo *supplied_admin_account)
{
    // The identity of the admin is loaded from the config account; ensure that this is the actual one true config
    // account
    if (!is_program_config_account(config_account->key)) {
        sol_log_pubkey(config_account->key);
        return false;
    }

    // The config account can be locked down to exactly the expected permissions because it is never going to be
    // used for any other purpose than reading config data.
    if (config_account->is_signer || config_account->is_writable || config_account->executable) {
        return false;
    }

    // The data must be correctly sized -- may be larger than, but never smaller than, the expected size
    if (config_account->data_len < sizeof(ProgramConfig)) {
        return false;
    }

    // Get a reference to the admin pubkey from the config
    ProgramConfig *config = (ProgramConfig *) (config_account->data);
    SolPubkey *admin_pubkey = &(config->admin_pubkey);

    // Now ensure that admin signer account is actually the configured admin account
    if (sol_memcmp(admin_pubkey, supplied_admin_account->key, sizeof(SolPubkey))) {
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
