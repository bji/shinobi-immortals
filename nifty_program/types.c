
// This is a 16-bit fixed point fraction: 8 bits.8 bits
typedef uint16_t fp16_t;


// This is the format of data stored in the program config account
typedef struct
{
    // Pubkey of the admin user, which is the only user allowed to perform admin functions
    SolPubkey admin_pubkey;

    // Program id of the "metadata program" which is used to create and update metadata.  This is configurable
    // and is updated whenever the program needs to be replaced.  This is because metadata is to some degree
    // outside of the control of this program, being the responsibility of the Metaplex metadata program, and
    // that program may change in a way that requires changes to how that program is used.  So when there is a need
    // to update the metadata program, a new metadata program id will be written here.  And then any user who agrees
    // to update the metadata reference for their owned NFTs can issue an UpdateMetadataProgramId instruction
    // which will update that NFT to use that metadata program (via the metadata program id stored in the entry).
    // In this way, the admin can "propose" a new metadata program to use, but only owners of NFTs can actually
    // switch to the new metadata program id.
    SolPubkey metadata_program_id;
} ProgramConfig;
