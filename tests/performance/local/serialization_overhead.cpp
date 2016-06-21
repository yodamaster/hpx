//  Copyright (c) 2007-2013 Hartmut Kaiser
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <hpx/hpx_init.hpp>
#include <hpx/include/actions.hpp>
#include <hpx/include/iostreams.hpp>
#include <hpx/include/serialization.hpp>
#include <hpx/runtime/serialization/detail/future_await_container.hpp>
#include <hpx/util/high_resolution_timer.hpp>

#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// This function will never be called
int test_function(hpx::serialization::serialize_buffer<double> const& b)
{
    return 42;
}
HPX_PLAIN_ACTION(test_function, test_action)

std::size_t get_archive_size(std::vector<hpx::parcelset::parcel> const& p,
    boost::uint32_t flags,
    std::vector<hpx::serialization::serialization_chunk>* chunks)
{
    // gather the required size for the archive
    hpx::serialization::detail::size_gatherer_container gather_size;
    hpx::serialization::output_archive archive(
        gather_size, flags, 0, chunks);
    archive << p;
    return gather_size.size();
}

void future_await(std::vector<hpx::parcelset::parcel> const& p)
{
    hpx::serialization::detail::future_await_container future_await;
    hpx::serialization::output_archive archive(future_await);
    archive << p;
}

struct timing
{
    timing()
      : average_total(0.0)
      , average_input(0.0)
      , average_output(0.0)
      , average_size(0.0)
      , average_future_await(0.0)
      , iterations(0)
    {}

    timing& operator+=(timing const& other)
    {
        average_total += other.average_total;
        average_input += other.average_input;
        average_output += other.average_output;
        average_size += other.average_size;
        average_future_await += other.average_future_await;
        iterations += other.iterations;

        return *this;
    }

    void report(std::size_t data_size, std::size_t verbose)
    {
        double const scale = 1e9;

        double total = (average_total / iterations) * scale;
        double input = (average_input / iterations) * scale;
        double output = (average_output / iterations) * scale;
        double size = (average_size / iterations) * scale;
        double future_await = (average_future_await / iterations) * scale;

        double input_perc = (average_input / average_total) * 100.0;
        double output_perc = (average_output / average_total) * 100.0;
        double size_perc = (average_size / average_total) * 100.0;
        double future_await_perc = (average_future_await / average_total) * 100.0;

        if(verbose == 1)
        {
            hpx::cout
                << "data size,"
                << "iterations,"
                << "total time [ns],"
                << "future await time [ns],"
                << "size calculation time [ns],"
                << "output archive time [ns],"
                << "input archive time [ns]\n"
                << "future await [%],"
                << "size calculation [%],"
                << "output archive [%],"
                << "input archive [%]\n"
                << hpx::flush;


        }

        if(verbose < 2)
        {
            hpx::cout
                << data_size * sizeof(double) << ","
                << iterations << ","
                << total << ","
                << future_await << ","
                << size << ","
                << output << ","
                << input << ","
                << future_await_perc << ","
                << size_perc << ","
                << output_perc << ","
                << input_perc
                << "\n" << hpx::flush;
        }

        if(verbose == 2)
        {
            hpx::cout << "Timings reported in nano seconds.\n"
                << "The test ran for 5 seconds and performed a total of "
                << iterations << " iterations.\n"
                << "Data size is " << data_size * sizeof(double) << " byte.\n"
                << "Total time per parcel (input and output): " << total << "\n"
                << " Future await calculation: " << future_await << " (" << future_await_perc << "%)\n"
                << " Size calculation: " << size << " (" << size_perc << "%)\n"
                << " Output archive time: " << output << " (" << output_perc << "%)\n"
                << " Input archive time: " << input << " (" << input_perc << "%)\n"
                << hpx::flush;
            return;
        }
    }

    double average_total;
    double average_input;
    double average_output;
    double average_size;
    double average_future_await;
    std::size_t iterations;
};

///////////////////////////////////////////////////////////////////////////////
timing benchmark_serialization(std::size_t data_size, std::size_t batch,
    bool continuation, bool zerocopy)
{
    hpx::naming::id_type const here = hpx::find_here();
    hpx::naming::address addr(hpx::get_locality(),
        hpx::components::component_invalid,
        reinterpret_cast<boost::uint64_t>(&test_function));

    // compose archive flags
#ifdef BOOST_BIG_ENDIAN
    std::string endian_out =
        hpx::get_config_entry("hpx.parcel.endian_out", "big");
#else
    std::string endian_out =
        hpx::get_config_entry("hpx.parcel.endian_out", "little");
#endif

    unsigned int out_archive_flags = 0U;
    if (endian_out == "little")
        out_archive_flags |= hpx::serialization::endian_little;
    else if (endian_out == "big")
        out_archive_flags |= hpx::serialization::endian_big;
    else {
        HPX_ASSERT(endian_out =="little" || endian_out == "big");
    }

    std::string array_optimization =
        hpx::get_config_entry("hpx.parcel.array_optimization", "1");

    if (boost::lexical_cast<int>(array_optimization) == 0)
    {
        out_archive_flags |= hpx::serialization::disable_array_optimization;
        out_archive_flags |= hpx::serialization::disable_data_chunking;
    }
    else
    {
        std::string zero_copy_optimization =
            hpx::get_config_entry("hpx.parcel.zero_copy_optimization", "1");
        if (boost::lexical_cast<int>(zero_copy_optimization) == 0)
        {
            out_archive_flags |= hpx::serialization::disable_data_chunking;
        }
    }

    // create argument for action
    std::vector<double> data;
    data.resize(data_size);

    hpx::serialization::serialize_buffer<double> buffer(data.data(), data.size(),
        hpx::serialization::serialize_buffer<double>::reference);

    // create parcels with/without continuation
    std::vector<hpx::parcelset::parcel> outp;
    outp.reserve(batch);
    for(std::size_t i = 0; i < batch; ++i)
    {
        if (continuation) {
            outp.push_back(hpx::parcelset::parcel(here, addr,
                hpx::actions::typed_continuation<int>(here),
                test_action(), hpx::threads::thread_priority_normal, buffer
                ));
        }
        else {
            outp.push_back(hpx::parcelset::parcel(here, addr,
                test_action(), hpx::threads::thread_priority_normal, buffer));
        }
        outp.back().parcel_id() = hpx::parcelset::parcel::generate_unique_id();
        outp.back().set_source_id(here);
    }

    std::vector<hpx::serialization::serialization_chunk>* chunks = nullptr;
    if (zerocopy)
        chunks = new std::vector<hpx::serialization::serialization_chunk>();

    boost::uint32_t dest_locality_id = outp.back().destination_locality_id();
    hpx::util::high_resolution_timer t;

    timing timings;

    // Run for 5 seconds
    while (t.elapsed() < 5.0)
    {
        double start = 0.0;

        start = t.now();
        future_await(outp);
        timings.average_future_await += t.now() - start;

        start = t.now();
        std::size_t arg_size = get_archive_size(outp, out_archive_flags, chunks);
        std::vector<char> out_buffer;
        timings.average_size += t.now() - start;

        out_buffer.resize(arg_size + HPX_PARCEL_SERIALIZATION_OVERHEAD);

        {
            start = t.now();
            // create an output archive and serialize the parcel
            hpx::serialization::output_archive archive(
                out_buffer, out_archive_flags, dest_locality_id, chunks);
            archive << batch;
            for(hpx::parcelset::parcel& p: outp)
            {
                archive << p;
            }
            //archive << outp;
            arg_size = archive.bytes_written();
            timings.average_output += t.now() - start;
        }

        {
            start = t.now();
            // create an input archive and deserialize the parcel
            hpx::serialization::input_archive archive(
                out_buffer, arg_size, chunks);

            std::size_t batch_size = 0;
            archive >> batch_size;
            for (std::size_t i = 0; i < batch_size; ++i)
            {
                hpx::parcelset::parcel p;
                archive >> p;
            }
            timings.average_input += t.now() - start;
        }

        if (chunks)
            chunks->clear();
        timings.iterations += batch;
    }
    timings.average_total = t.elapsed();

    return timings;
}

///////////////////////////////////////////////////////////////////////////////
std::size_t data_size = 1;
std::size_t concurrency = 1;
std::size_t batch = 1;
std::size_t verbose = 2;

int hpx_main(boost::program_options::variables_map& vm)
{
    bool continuation = vm.count("continuation") != 0;
    bool zerocopy = vm.count("zerocopy") != 0;

    std::vector<hpx::future<timing> > timings;
    for (std::size_t i = 0; i != concurrency; ++i)
    {
        timings.push_back(hpx::async(
            &benchmark_serialization, data_size, batch,
            continuation, zerocopy));
    }

    timing overall_time;
    for (std::size_t i = 0; i != concurrency; ++i)
        overall_time += timings[i].get();

    overall_time.report(data_size, verbose);

    return hpx::finalize();
}

int main(int argc, char* argv[])
{
    // Configure application-specific options.
    boost::program_options::options_description cmdline(
        "usage: " HPX_APPLICATION_STRING " [options]");

    cmdline.add_options()
        ( "concurrency"
        , boost::program_options::value<std::size_t>(&concurrency)->default_value(1)
        , "number of concurrent serialization operations (default: 1)")

        ( "data_size"
        , boost::program_options::value<std::size_t>(&data_size)->default_value(1)
        , "size of data buffer to serialize in bytes (default: 1)")

        ( "batch"
        , boost::program_options::value<std::size_t>(&batch)->default_value(1)
        , "number of parcels to batch in one call to serialization (default: 1)")

        ( "continuation"
        , "add a continuation to each created parcel")

        ( "zerocopy"
        , "use zero copy serialization of bitwise copyable arguments")

        ( "verbose"
        , boost::program_options::value<std::size_t>(&verbose)->default_value(2)
        , "Verbosity of the output report.\n"
          "0: print cvs, 1: print cvs with header, 2: human readable")
        ;

    return hpx::init(cmdline, argc, argv);
}

