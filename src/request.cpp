#include "request.h"

void ReadRequest::post(OneSidedClient &client) {
    client.post_read(offset, len);
}

void RMCRequestHandler::post(ReadRequest &req) {
    assert(next_rmc != nullptr);
    
    if (is_mem_ready()) {
        if (!batch_in_progress)
            start_batched_ops();

        req.post(*client);
        mem_queue->push(next_rmc);

        if (!is_mem_ready())
            end_batched_ops();
    } else {
        buffer_queue->emplace(next_rmc, req);
    }

    /* ensures that we do not call push_read again unless the scheduler knows about it */
    next_rmc = nullptr;
}

void RMCRequestHandler::post_read(uint32_t offset, uint32_t len) {
    auto req = ReadRequest(offset, len);
    post(req);
    /*
    assert(next_rmc != nullptr);
    
    if (is_mem_ready()) {
        if (!batch_in_progress)
            start_batched_ops();

        client->post_read(offset, len);
        mem_queue->push(next_rmc);

        if (!is_mem_ready())
            end_batched_ops();
    } else {
        auto req = ReadRequest(offset, len);
        buffer_queue->emplace(next_rmc, req);
    }

    //  Ensures that we do not call push_read again unless the scheduler knows about it 
    next_rmc = nullptr;
    */
}

void RMCRequestHandler::start_rmc_processing(CoroRMC<int> *rmc) {

    assert(next_rmc == nullptr);
    next_rmc = rmc;
}

void RMCRequestHandler::end_rmc_processing(bool is_rmc_done) {
    /* Occurs if the rmc was added to the mem_queue or buffer_queue */
    if (next_rmc == nullptr) {
        return;
    } 

    next_rmc = nullptr;
    if (!is_rmc_done)
        run_queue->push(next_rmc);
}

void RMCRequestHandler::start_batched_ops() {
    if (batch_in_progress)
        return;

    client->start_batched_ops();
    batch_in_progress = true;
}

void RMCRequestHandler::end_batched_ops() {
    if (!batch_in_progress)
        return;

    client->end_batched_ops();
    batch_in_progress = false;
}
