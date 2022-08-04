#pragma once

static constexpr const unsigned int MB = 1 * 1024 * 1024;
static constexpr const uint16_t PAGE_SIZE = 4096;
static constexpr const uint64_t RMCK_TOTAL_BUFF_SZ = 1024 * MB;
/* reserved space for e.g. locks */
static constexpr const uint64_t RMCK_RESERVED_BUFF_SZ = 16 * MB;
static constexpr const uint64_t RMCK_APPS_BUFF_SZ =
    RMCK_TOTAL_BUFF_SZ - RMCK_RESERVED_BUFF_SZ;
/* rmc arguments */
static constexpr const unsigned MAX_EXECREQ_DATA = 256;
/* to store RMC reply results */
static constexpr const size_t MAX_RMC_REPLY_LEN = 16;
/* how many RMCs we are going to register */
static constexpr uint8_t NUM_REG_RMC = 1;
