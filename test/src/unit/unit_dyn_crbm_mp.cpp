//=======================================================================
// Copyright (c) 2014-2017 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#include <numeric>

#include "dll_test.hpp"

#include "cpp_utils/data.hpp"

#include "dll/rbm/dyn_conv_rbm_mp.hpp"

#include "mnist/mnist_reader.hpp"
#include "mnist/mnist_utils.hpp"

TEST_CASE("unit/dyn_crbm_mp/mnist/1", "[dyn_crbm_mp][unit]") {
    dll::dyn_conv_rbm_mp_desc<
        dll::weight_type<float>,
        dll::momentum,
        dll::serial,
        dll::parallel_mode>::layer_t rbm;

    rbm.init_layer(1, 28, 28, 20, 17, 17, 2);

    auto dataset = mnist::read_dataset_direct<std::vector, etl::fast_dyn_matrix<float, 1, 28, 28>>(100);
    REQUIRE(!dataset.training_images.empty());

    mnist::binarize_dataset(dataset);

    auto error = rbm.train(dataset.training_images, 40);
    REQUIRE(error < 5e-2);

    rbm.v1 = dataset.training_images[1];

    rbm.template activate_hidden<true, false>(rbm.h1_a, rbm.h1_a, rbm.v1, rbm.v1);

    auto energy = rbm.energy(dataset.training_images[1], rbm.h1_a);
    REQUIRE(energy < 0.0);

    auto free_energy = rbm.free_energy();
    REQUIRE(free_energy < 0.0);
}

TEST_CASE("unit/dyn_crbm_mp/mnist/3", "[dyn_crbm_mp][denoising][unit]") {
    dll::dyn_conv_rbm_mp_desc<
        dll::momentum,
        dll::weight_decay<dll::decay_type::L2>,
        dll::visible<dll::unit_type::GAUSSIAN>,
        dll::shuffle>::layer_t rbm;

    rbm.init_layer(1, 28, 28, 30, 17, 17, 2);

    rbm.learning_rate *= 4;

    auto dataset = mnist::read_dataset_direct<std::vector, etl::fast_dyn_matrix<float, 1, 28, 28>>(100);
    REQUIRE(!dataset.training_images.empty());

    mnist::normalize_dataset(dataset);

    auto noisy = dataset.training_images;

    std::default_random_engine rand_engine(56);
    std::normal_distribution<double> normal_distribution(0.0, 0.1);
    auto noise = std::bind(normal_distribution, rand_engine);

    for (auto& image : noisy) {
        for (auto& noisy_x : image) {
            noisy_x += noise();
        }
    }

    cpp::normalize_each(noisy);

    auto error = rbm.train_denoising(noisy, dataset.training_images, 50);
    REQUIRE(error < 0.3);
    cpp_unused(error);
}

TEST_CASE("unit/dyn_crbm_mp/mnist/4", "[dyn_crbm_mp][relu][unit]") {
    dll::dyn_conv_rbm_mp_desc<dll::hidden<dll::unit_type::RELU>>::layer_t rbm;

    rbm.init_layer(1, 28, 28, 20, 17, 17, 2);
    rbm.learning_rate *= 2;

    auto dataset = mnist::read_dataset_direct<std::vector, etl::fast_dyn_matrix<float, 1, 28, 28>>(100);
    REQUIRE(!dataset.training_images.empty());

    mnist::binarize_dataset(dataset);

    auto error = rbm.train(dataset.training_images, 50);
    REQUIRE(error < 5e-2);
}
