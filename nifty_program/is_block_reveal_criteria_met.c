

static bool is_block_reveal_criteria_met(Block *block, Clock *clock)
{
    // If it's beyond the ticketing phase, then the reveal criteria are met
    if ((block->state.block_start_timestamp + block->config.ticket_phase_duration) > clock->unix_timestamp) {
        return true;
    }

    // If all the tickets have been sold, then the reveal criteria are met
    if (block->state.tickets_sold_count == block->config.total_ticket_count) {
        return true;
    }

    // Reveal criteria are not met
    return false;
}
