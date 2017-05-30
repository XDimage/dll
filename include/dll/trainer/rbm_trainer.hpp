//=======================================================================
// Copyright (c) 2014-2017 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#pragma once

#include <memory>

#include "cpp_utils/algorithm.hpp"
#include "cpp_utils/static_if.hpp"

#include "dll/decay_type.hpp"
#include "dll/util/batch.hpp"
#include "dll/util/timers.hpp"
#include "dll/util/random.hpp"
#include "dll/layer_traits.hpp"
#include "dll/trainer/rbm_trainer_fwd.hpp"
#include "dll/trainer/rbm_training_context.hpp"

namespace dll {

enum class init_watcher_t { INIT };
constexpr init_watcher_t init_watcher = init_watcher_t::INIT;

template <typename RBM, typename RW, typename Enable = void>
struct watcher_type {
    using watcher_t = typename RBM::desc::template watcher_t<RBM>;
};

template <typename RBM, typename RW>
struct watcher_type<RBM, RW, std::enable_if_t<cpp::not_u<std::is_void<RW>::value>::value>> {
    using watcher_t = RW;
};

/*!
 * \brief A generic trainer for Restricted Boltzmann Machine
 *
 * This trainer use the specified trainer of the RBM to perform unsupervised
 * training.
 */
template <typename RBM, bool EnableWatcher, typename RW, bool Denoising>
struct rbm_trainer {
    using rbm_t = RBM;
    using error_type = typename rbm_t::weight;

    template <typename R>
    using trainer_t = typename rbm_t::desc::template trainer_t<R, Denoising>;

    using trainer_type = std::unique_ptr<trainer_t<rbm_t>>;

    using watcher_t = typename watcher_type<rbm_t, RW>::watcher_t;

    mutable watcher_t watcher;

    rbm_trainer()
            : watcher() {}

    template <typename... Arg>
    rbm_trainer(init_watcher_t /*init*/, Arg... args)
            : watcher(args...) {}

    template <typename Iterator, cpp_enable_if_cst(rbm_layer_traits<rbm_t>::init_weights())>
    static void init_weights(RBM& rbm, Iterator first, Iterator last) {
        rbm.init_weights(first, last);
    }

    template <typename Iterator, cpp_disable_if_cst(rbm_layer_traits<rbm_t>::init_weights())>
    static void init_weights(RBM&, Iterator, Iterator) {
        //NOP
    }

    template <typename Generator, cpp_enable_if_cst(rbm_layer_traits<rbm_t>::init_weights())>
    static void init_weights(RBM& rbm, Generator& generator) {
        rbm.init_weights(generator);
    }

    template <typename Generator, cpp_disable_if_cst(rbm_layer_traits<rbm_t>::init_weights())>
    static void init_weights(RBM&, Generator&) {
        //NOP
    }

    template <typename IIterator, cpp_enable_if_cst(rbm_layer_traits<rbm_t>::has_shuffle())>
    static void shuffle_direct(IIterator ifirst, IIterator ilast) {
        decltype(auto) g = dll::rand_engine();
        std::shuffle(ifirst, ilast, g);
    }

    template <typename IIterator, cpp_disable_if_cst(rbm_layer_traits<rbm_t>::has_shuffle())>
    static void shuffle_direct(IIterator, IIterator) {}

    template <typename IIterator, typename EIterator, cpp_enable_if_cst(rbm_layer_traits<rbm_t>::has_shuffle())>
    static void shuffle(IIterator ifirst, IIterator ilast, EIterator efirst, EIterator elast) {
        decltype(auto) g = dll::rand_engine();
        if (Denoising) {
            cpp::parallel_shuffle(ifirst, ilast, efirst, elast, g);
        } else {
            std::shuffle(ifirst, ilast, g);
        }
    }

    template <typename IIterator, typename EIterator, cpp_disable_if_cst(rbm_layer_traits<rbm_t>::has_shuffle())>
    static void shuffle(IIterator, IIterator, EIterator, EIterator) {}

    template <typename IIterator, typename EIterator, typename IVector, typename EVector, cpp_enable_if_cst(rbm_layer_traits<rbm_t>::has_shuffle())>
    static auto prepare_it(IIterator ifirst, IIterator ilast, EIterator efirst, EIterator elast, IVector& ivec, EVector& evec) {
        std::copy(ifirst, ilast, std::back_inserter(ivec));

        auto input_first = ivec.begin();
        auto input_last  = ivec.end();

        if (Denoising) {
            std::copy(efirst, elast, std::back_inserter(evec));

            auto expected_first = evec.begin();
            auto expected_last = evec.end();
            return std::make_tuple(input_first, input_last, expected_first, expected_last);
        } else {
            return std::make_tuple(input_first, input_last, input_first, input_last);
        }
    }

    template <typename IIterator, typename EIterator, typename IVector, typename EVector, cpp_disable_if_cst(rbm_layer_traits<rbm_t>::has_shuffle())>
    static auto prepare_it(IIterator ifirst, IIterator ilast, EIterator efirst, EIterator elast, IVector&, EVector&) {
        return std::make_tuple(ifirst, ilast, efirst, elast);
    }

    template <typename rbm_t, typename Iterator>
    using fix_iterator_t = std::conditional_t<
        rbm_layer_traits<rbm_t>::has_shuffle(),
        typename std::vector<typename std::iterator_traits<Iterator>::value_type>::iterator,
        Iterator>;

    size_t batch_size            = 0;
    size_t total_batches         = 0;
    error_type last_error = 0.0;

    //Note: input_first/input_last only relevant for its size, not
    //values since they can point to the input of the first level
    //and not the current level
    template <typename Iterator>
    void init_training(RBM& rbm, Iterator input_first, Iterator input_last) {
        rbm.momentum = rbm.initial_momentum;

        if (EnableWatcher) {
            watcher.training_begin(rbm);
        }

        //Get the size of each batches
        batch_size = get_batch_size(rbm);

        if (std::is_same<typename std::iterator_traits<Iterator>::iterator_category, std::random_access_iterator_tag>::value) {
            auto size = distance(input_first, input_last);

            //TODO Better handling of incomplete batch size would solve this problem (this could be done by
            //cleaning the data before the last batch)
            if (size % batch_size != 0) {
#ifndef DLL_SILENT
                std::cout << "WARNING: The number of samples should be divisible by the batch size" << std::endl;
                std::cout << "         This may cause discrepancies in the results." << std::endl;
#endif
            }

            //Only used for debugging purposes, no need to be precise
            total_batches = size / batch_size;
        } else {
            total_batches = 0;
        }

        last_error = 0.0;
    }

    //Note: input_first/input_last only relevant for its size, not
    //values since they can point to the input of the first level
    //and not the current level
    template <typename Generator>
    void init_training(RBM& rbm, Generator& generator) {
        rbm.momentum = rbm.initial_momentum;

        if (EnableWatcher) {
            watcher.training_begin(rbm);
        }

        //Get the size of each batches
        batch_size = get_batch_size(rbm);

        auto size = generator.size();

        //TODO Better handling of incomplete batch size would solve this problem (this could be done by
        //cleaning the data before the last batch)
        if (size % batch_size != 0) {
#ifndef DLL_SILENT
            std::cout << "WARNING: The number of samples should be divisible by the batch size" << std::endl;
            std::cout << "         This may cause discrepancies in the results." << std::endl;
#endif
        }

        //Only used for debugging purposes, no need to be precise
        total_batches = size / batch_size;

        last_error = 0.0;
    }

    template <typename Iterator>
    error_type train(RBM& rbm, Iterator first, Iterator last, size_t max_epochs) {
        return train(rbm, first, last, first, last, max_epochs);
    }

    static trainer_type get_trainer(RBM& rbm) {
        //Allocate the trainer on the heap (may be large)
        return std::make_unique<trainer_t<rbm_t>>(rbm);
    }

    error_type finalize_training(RBM& rbm) {
        if (EnableWatcher) {
            watcher.training_end(rbm);
        }

        return last_error;
    }

    template <typename IIterator, typename EIterator>
    error_type train(RBM& rbm, IIterator ifirst, IIterator ilast, EIterator efirst, EIterator elast, size_t max_epochs) {
        dll::auto_timer timer("rbm_trainer:train");

        //In case of shuffle, we don't want to shuffle the input, therefore create a copy and shuffle it

        std::vector<typename std::iterator_traits<IIterator>::value_type> input_copy;
        std::vector<typename std::iterator_traits<EIterator>::value_type> expected_copy;

        auto iterators = prepare_it(ifirst, ilast, efirst, elast, input_copy, expected_copy);

        decltype(auto) input_first = std::get<0>(iterators);
        decltype(auto) input_last = std::get<1>(iterators);

        decltype(auto) expected_first = std::get<2>(iterators);
        decltype(auto) expected_last = std::get<3>(iterators);

        //Initialize RBM and trainign parameters
        init_training(rbm, input_first, input_last);

        //Some RBM may init weights based on the training data
        //Note: This can't be done in init_training, since it will
        //sometimes be called with the wrong input values
        init_weights(rbm, input_first, input_last);

        //Allocate the trainer
        auto trainer = get_trainer(rbm);

        //Train for max_epochs epoch
        for (size_t epoch = 0; epoch < max_epochs; ++epoch) {
            //Shuffle if necessary
            shuffle(input_first, input_last, expected_first, expected_last);

            //Create a new context for this epoch
            rbm_training_context context;

            //Start a new epoch
            init_epoch();

            //Train on all the data
            train_sub(input_first, input_last, expected_first, trainer, context, rbm);

            //Finalize the current epoch
            finalize_epoch(epoch, context, rbm);
        }

        return finalize_training(rbm);
    }

    template <typename Generator>
    error_type train(RBM& rbm, Generator & generator, size_t max_epochs) {
        dll::auto_timer timer("rbm_trainer:train");

        //Initialize RBM and trainign parameters
        init_training(rbm, generator);

        //Some RBM may init weights based on the training data
        //Note: This can't be done in init_training, since it will
        //sometimes be called with the wrong input values
        init_weights(rbm, generator);

        //Allocate the trainer
        auto trainer = get_trainer(rbm);

        //Train for max_epochs epoch
        for (size_t epoch = 0; epoch < max_epochs; ++epoch) {
            //Shuffle if necessary
            if(rbm_layer_traits<rbm_t>::has_shuffle()){
                generator.reset_shuffle();
            } else {
                generator.reset();
            }

            // Set the the generator in train mode
            generator.set_train();

            //Create a new context for this epoch
            rbm_training_context context;

            //Start a new epoch
            init_epoch();

            //Train on all the data
            train_sub(generator, trainer, context, rbm);

            //Finalize the current epoch
            finalize_epoch(epoch, context, rbm);
        }

        return finalize_training(rbm);
    }

    template <typename IIterator>
    error_type train_denoising_auto(RBM& rbm, IIterator ifirst, IIterator ilast, size_t max_epochs, double noise) {
        dll::auto_timer timer("rbm_trainer:train:auto");

        cpp_assert(!Denoising, "train_denoising_auto should not set Denoising");

        //In case of shuffle, we don't want to shuffle the input, therefore create a copy and shuffle it

        auto n = std::distance(ifirst, ilast);
        std::vector<typename std::iterator_traits<IIterator>::value_type> input_clean(n);
        std::vector<typename std::iterator_traits<IIterator>::value_type> input_copy(n);

        std::copy(ifirst, ilast, input_clean.begin());

        //Initialize RBM and trainign parameters
        init_training(rbm, input_clean.begin(), input_clean.end());

        //Some RBM may init weights based on the training data
        //Note: This can't be done in init_training, since it will
        //sometimes be called with the wrong input values
        init_weights(rbm, input_clean.begin(), input_clean.end());

        //Allocate the trainer
        auto trainer = get_trainer(rbm);

        auto input_transformer = [noise](auto&& value){
            decltype(auto) g = dll::rand_engine();

            std::uniform_real_distribution<double> dist(0.0, 1000.0);

            for(auto& v :  value){
                v *= dist(g) < noise * 1000.0 ? 0.0 : 1.0;
            }
        };

        //Train for max_epochs epoch
        for (size_t epoch = 0; epoch < max_epochs; ++epoch) {
            //Shuffle if necessary
            shuffle_direct(input_clean.begin(), input_clean.end());

            // Copy the input
            std::copy(input_clean.begin(), input_clean.end(), input_copy.begin());

            // Corrupt the input
            for(auto& input : input_copy){
                input_transformer(input);
            }

            //Create a new context for this epoch
            rbm_training_context context;

            //Start a new epoch
            init_epoch();

            //Train on all the data
            train_sub(input_copy.begin(), input_copy.end(), input_clean.begin(), trainer, context, rbm);

            //Finalize the current epoch
            finalize_epoch(epoch, context, rbm);
        }

        return finalize_training(rbm);
    }

    size_t batches = 0;
    size_t samples = 0;

    void init_epoch() {
        batches = 0;
        samples = 0;
    }

    template <typename Generator>
    void train_sub(Generator& generator, trainer_type& trainer, rbm_training_context& context, rbm_t& rbm) {
        while(generator.has_next_batch()){
            //Train the batch
            train_batch(generator.data_batch(), generator.label_batch(), trainer, context, rbm);

            // Go to the next batch
            generator.next_batch();
        }
    }

    template <typename IIT, typename EIT>
    void train_sub(IIT input_first, IIT input_last, EIT expected_first, trainer_type& trainer, rbm_training_context& context, rbm_t& rbm) {
        auto iit = input_first;
        auto eit = expected_first;
        auto end = input_last;

        while (iit != end) {
            auto istart = iit;
            auto estart = eit;

            size_t i = 0;
            while (iit != end && i < batch_size) {
                ++iit;
                ++eit;
                ++samples;
                ++i;
            }

            //Train the batch
            train_batch(istart, iit, estart, eit, trainer, context, rbm);
        }
    }

    template <typename InputBatch, typename ExpectedBatch>
    void train_batch(InputBatch&& input, ExpectedBatch&& expected, trainer_type& trainer, rbm_training_context& context, rbm_t& rbm) {
        ++batches;

        trainer->train_batch(input, expected, context);

        context.reconstruction_error += context.batch_error;
        context.sparsity += context.batch_sparsity;

        cpp::static_if<EnableWatcher && rbm_layer_traits<rbm_t>::free_energy()>([&](auto f) {
            for (auto& v : input) {
                context.free_energy += f(rbm).free_energy(v);
            }
        });

        if (EnableWatcher && rbm_layer_traits<rbm_t>::is_verbose()) {
            watcher.batch_end(rbm, context, batches, total_batches);
        }
    }

    template <typename IIT, typename EIT>
    void train_batch(IIT input_first, IIT input_last, EIT expected_first, EIT expected_last, trainer_type& trainer, rbm_training_context& context, rbm_t& rbm) {
        ++batches;

        auto input_batch    = make_batch(input_first, input_last);
        auto expected_batch = make_batch(expected_first, expected_last);
        trainer->train_batch(input_batch, expected_batch, context);

        context.reconstruction_error += context.batch_error;
        context.sparsity += context.batch_sparsity;

        cpp::static_if<EnableWatcher && rbm_layer_traits<rbm_t>::free_energy()>([&](auto f) {
            for (auto& v : input_batch) {
                context.free_energy += f(rbm).free_energy(v);
            }
        });

        if (EnableWatcher && rbm_layer_traits<rbm_t>::is_verbose()) {
            watcher.batch_end(rbm, context, batches, total_batches);
        }
    }

    void finalize_epoch(size_t epoch, rbm_training_context& context, rbm_t& rbm) {
        //Average all the gathered information
        context.reconstruction_error /= batches;
        context.sparsity /= batches;
        context.free_energy /= samples;

        //After some time increase the momentum
        if (rbm_layer_traits<rbm_t>::has_momentum() && epoch == rbm.final_momentum_epoch) {
            rbm.momentum = rbm.final_momentum;
        }

        //Notify the watcher
        if (EnableWatcher) {
            watcher.epoch_end(epoch, context, rbm);
        }

        //Save the error for the return value
        last_error = context.reconstruction_error;
    }
};

} //end of dll namespace
