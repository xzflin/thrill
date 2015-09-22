/*******************************************************************************
 * benchmarks/data/file_read_write.cpp
 *
 * Part of Project Thrill.
 *
 * Copyright (C) 2015 Tobias Sturm <mail@tobiassturm.de>
 *
 * This file has no license. Only Chuck Norris can compile it.
 ******************************************************************************/

#include <thrill/api/context.hpp>
#include <thrill/data/block_queue.hpp>
#include <thrill/common/cmdline_parser.hpp>
#include <thrill/common/logger.hpp>
#include <thrill/common/thread_pool.hpp>
#include <thrill/common/stats_timer.hpp>

#include <iostream>
#include <random>
#include <string>
#include <tuple>

#include "data_generators.hpp"

using namespace thrill; // NOLINT
using common::StatsTimer;

//! Writes and reads random elements from a file.
//! Elements are genreated before the timer startet
//! Number of elements depends on the number of bytes.
//! one RESULT line will be printed for each iteration
//! All iterations use the same generated data.
//! Variable-length elements range between 1 and 100 bytes per default
template <typename Type>
void FileExperiment(uint64_t bytes, size_t min_size, size_t max_size, unsigned iterations, api::Context& ctx, const std::string& type_as_string, const std::string& reader_type, size_t block_size) {

    if (reader_type != "consume" && reader_type != "non-consume")
        abort();

    for (unsigned i = 0; i < iterations; i++) {
        auto file = ctx.GetFile();
        auto writer = file.GetWriter(block_size);
        auto data = Generator<Type>(bytes, min_size, max_size);

        std::cout << "writing " << bytes << " bytes" << std::endl;
        StatsTimer<true> write_timer(true);
        while (data.HasNext()) {
            writer(data.Next());
        }
        writer.Close();
        write_timer.Stop();

        std::cout << "reading " << bytes << " bytes" << std::endl;
        bool consume = reader_type == "consume";
        StatsTimer<true> read_timer(true);
        auto reader = file.GetReader(consume);
        while (reader.HasNext())
            reader.Next<Type>();
        read_timer.Stop();
        std::cout << "RESULT"
                  << " experiment=" << "file"
                  << " datatype=" << type_as_string
                  << " size=" << bytes
                  << " block_size=" << block_size
                  << " avg_element_size=" << (min_size + max_size) / 2.0
                  << " reader=" << reader_type
                  << " write_time=" << write_timer.Microseconds()
                  << " read_time=" << read_timer.Microseconds()
                  << std::endl;
    }
}

//! Writes and reads random elements from a file.
//! Elements are genreated before the timer startet
//! Number of elements depends on the number of bytes.
//! one RESULT line will be printed for each iteration
//! All iterations use the same generated data.
//! Variable-length elements range between 1 and 100 bytes per default
template <typename Type>
void ChannelAToBExperiment(uint64_t bytes, size_t min_size, size_t max_size, unsigned iterations, api::Context& ctx, const std::string& type_as_string, size_t block_size) {

    for (unsigned i = 0; i < iterations; i++) {
        auto stream = ctx.GetNewCatStream();
        auto writers = stream->OpenWriters(block_size);
        auto data = Generator<Type>(bytes, min_size, max_size);

        StatsTimer<true> write_timer;
        if (ctx.my_rank() != 0) {
            std::cout << "writing " << bytes << " bytes" << std::endl;
            write_timer.Start();
            while (data.HasNext()) {
                writers[0](data.Next());
            }
            for (auto& w : writers)
                w.Close();
            write_timer.Stop();
        } else
            for (auto& w : writers)
                w.Close();

        StatsTimer<true> read_timer;
        if (ctx.my_rank() == 0) {
            std::cout << "reading " << bytes << " bytes" << std::endl;
            read_timer.Start();
            auto reader = stream->OpenCatReader(true/*consume*/);
            while (reader.HasNext())
                reader.Next<Type>();
            read_timer.Stop();
        }
        std::cout << "RESULT"
                  << " experiment=" << "file"
                  << " datatype=" << type_as_string
                  << " size=" << bytes
                  << " block_size=" << block_size
                  << " avg_element_size=" << (min_size + max_size) / 2.0
                  << " write_time=" << write_timer.Microseconds()
                  << " read_time=" << read_timer.Microseconds()
                  << std::endl;
    }
}

//! Writes and reads random elements to / from block queue with 2 threads
//! Elements are genreated before the timer startet
//! Number of elements depends on the number of bytes.
//! one RESULT line will be printed for each iteration
//! All iterations use the same generated data.
//! Variable-length elements range between 1 and 100 bytes per default
template <typename Type>
void BlockQueueExperiment(uint64_t bytes, size_t min_size, size_t max_size, unsigned iterations, api::Context& ctx, const std::string& type_as_string, const std::string& reader_type, size_t block_size, size_t num_threads) {

    if (reader_type != "consume" && reader_type != "non-consume")
        abort();

    common::ThreadPool threads(num_threads + 1);
    for (unsigned i = 0; i < iterations; i++) {
        auto queue = data::BlockQueue(ctx.block_pool());
        auto data = Generator<Type>(bytes, min_size, max_size);

        StatsTimer<true> write_timer;
        threads.Enqueue([bytes, &data, &write_timer, &queue, block_size]() {
            std::cout << "writing " << bytes << " bytes" << std::endl;
            auto writer = queue.GetWriter(block_size);
            write_timer.Start();
            while (data.HasNext()) {
                writer(data.Next());
            }
            writer.Close();
            write_timer.Stop();
        });

        std::chrono::microseconds::rep read_time = 0;
        bool consume = reader_type == "consume";
        for(size_t thread = 0; thread < num_threads; thread++) {
            threads.Enqueue([bytes, consume, &queue, &read_time]() {
                std::cout << "reading " << bytes << " bytes" << std::endl;
                StatsTimer<true> read_timer(true);
                auto reader = queue.GetReader(consume);
                while (reader.HasNext())
                    reader.Next<Type>();
                read_timer.Stop();
                read_time = std::max(read_time, read_timer.Microseconds());
            });
        }
        threads.LoopUntilEmpty();
        std::cout << "RESULT"
                  << " experiment=" << "block_queue"
                  << " datatype=" << type_as_string
                  << " size=" << bytes
                  << " block_size=" << block_size
                  << " avg_element_size=" << (min_size + max_size) / 2.0
                  << " reader=" << reader_type
                  << " write_time=" << write_timer.Microseconds()
                  << " read_time=" << read_time
                  << " threads=" << num_threads
                  << std::endl;
    }
}

int main(int argc, const char** argv) {
    common::NameThisThread("benchmark");

    common::CmdlineParser clp;
    clp.SetDescription("thrill::data benchmark for disk I/O");
    clp.SetAuthor("Tobias Sturm <mail@tobiassturm.de>");
    unsigned iterations = 1;
    unsigned threads = 1;
    uint64_t bytes = 1024;
    uint64_t block_size = data::default_block_size;
    uint64_t min_variable_length = 1;
    uint64_t max_variable_length = 100;
    std::string experiment;
    std::string type;
    std::string reader_type;
    clp.AddBytes('b', "bytes", bytes, "number of bytes to process (default 1024)");
    clp.AddBytes('s', "block_size", block_size, "block size (system default)");
    clp.AddBytes('l', "lower", min_variable_length, "lower bound for variable element length (default 1)");
    clp.AddBytes('u', "upper", max_variable_length, "upper bound for variable element length (default 100)");
    clp.AddUInt('n', "iterations", iterations, "Iterations (default: 1)");
    clp.AddUInt('t', "threads", threads, "Threads (default: 1)");
    clp.AddParamString("experiment", experiment,
                       "experiment to run (file, block_queue)");
    clp.AddParamString("type", type,
                       "data type (size_t, string, pair, triple)");
    clp.AddParamString("reader", reader_type,
                       "reader type (consume, non-consume)");
    if (!clp.Process(argc, argv)) return -1;

    using pair = std::tuple<std::string, size_t>;
    using triple = std::tuple<std::string, size_t, std::string>;

    if (experiment == "file") {
        if (type == "size_t")
            api::RunLocalSameThread(std::bind(FileExperiment<size_t>, bytes, min_variable_length, max_variable_length, iterations, std::placeholders::_1, type, reader_type, block_size));
        else if (type == "string")
            api::RunLocalSameThread(std::bind(FileExperiment<std::string>, bytes, min_variable_length, max_variable_length, iterations, std::placeholders::_1, type, reader_type, block_size));
        else if (type == "pair")
            api::RunLocalSameThread(std::bind(FileExperiment<pair>, bytes, min_variable_length, max_variable_length, iterations, std::placeholders::_1, type, reader_type, block_size));
        else if (type == "triple")
            api::RunLocalSameThread(std::bind(FileExperiment<triple>, bytes, min_variable_length, max_variable_length, iterations, std::placeholders::_1, type, reader_type, block_size));
        else
            abort();
    } else if (experiment == "block_queue") {
        if (type == "size_t")
            api::RunLocalSameThread(std::bind(BlockQueueExperiment<size_t>, bytes, min_variable_length, max_variable_length, iterations, std::placeholders::_1, type, reader_type, block_size, threads));
        else if (type == "string")
            api::RunLocalSameThread(std::bind(BlockQueueExperiment<std::string>, bytes, min_variable_length, max_variable_length, iterations, std::placeholders::_1, type, reader_type, block_size, threads));
        else if (type == "pair")
            api::RunLocalSameThread(std::bind(BlockQueueExperiment<pair>, bytes, min_variable_length, max_variable_length, iterations, std::placeholders::_1, type, reader_type, block_size, threads));
        else if (type == "triple")
            api::RunLocalSameThread(std::bind(BlockQueueExperiment<triple>, bytes, min_variable_length, max_variable_length, iterations, std::placeholders::_1, type, reader_type, block_size, threads));
        else
            abort();
    } else if (experiment == "channel_a_b") {
        if (type == "size_t")
            api::Run(std::bind(ChannelAToBExperiment<size_t>, bytes, min_variable_length, max_variable_length, iterations, std::placeholders::_1, type, block_size));
        else if (type == "string")
            api::Run(std::bind(ChannelAToBExperiment<std::string>, bytes, min_variable_length, max_variable_length, iterations, std::placeholders::_1, type, block_size));
        else if (type == "pair")
            api::Run(std::bind(ChannelAToBExperiment<pair>, bytes, min_variable_length, max_variable_length, iterations, std::placeholders::_1, type, block_size));
        else if (type == "triple")
            api::Run(std::bind(ChannelAToBExperiment<triple>, bytes, min_variable_length, max_variable_length, iterations, std::placeholders::_1, type, block_size));
        else
            abort();
    } else
        abort();
}

/******************************************************************************/