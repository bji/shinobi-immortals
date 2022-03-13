// Some instructions can only be invoked if the transaction was signed by the admin user.  The admin user
// is specified in the program config account.  For all instructions which require admin privileges, the
// first account of the instruction must be the program config account, and must be specified read-only, and
// the second account identifies the admin as a signer of the transaction.
static bool is_admin_authenticated(const SolAccountInfo *config_account,
                                   const SolAccountInfo *admin_signer_account)
{
    // The config account must be the correct account
    if (!is_program_config_account(config_account->key)) {
        return false;
    }

    // The instruction must only reference the config_account read-only, just as a safety measure, to prevent possible
    // bugs from causing it to be modified, and it must not be executable for a similar reason
    if (config_account->is_writable || config_account->executable) {
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
    if (sol_memcmp(admin_pubkey, admin_signer_account->key, sizeof(SolPubkey))) {
        return false;
    }

    // The admin signer account must be a read-only signer that is not executable
    if (admin_signer_account->is_writable || admin_signer_account->executable || !admin_signer_account->is_signer) {
        return false;
    }

    // All checks passed.  This transaction was signed by the admin account.
    return true;
}
