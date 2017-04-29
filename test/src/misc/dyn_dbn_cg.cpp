//=======================================================================
// Copyright (c) 2014-2017 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#include <deque>

#include "catch.hpp"

#define DLL_SVM_SUPPORT

#include "dll/rbm/dyn_rbm.hpp"
#include "dll/dbn.hpp"
#include "dll/trainer/conjugate_gradient.hpp"

#include "mnist/mnist_reader.hpp"
#include "mnist/mnist_utils.hpp"

TEST_CASE("dyn_dbn/cg/mnist/1", "dbn::simple") {
    using dbn_t =
        dll::dbn_desc<
            dll::dbn_layers<
                dll::dyn_rbm_desc<dll::momentum, dll::init_weights>::layer_t,
                dll::dyn_rbm_desc<dll::momentum>::layer_t,
                dll::dyn_rbm_desc<dll::momentum, dll::hidden<dll::unit_type::SOFTMAX>>::layer_t>,
            dll::batch_size<50>>::dbn_t;

    auto dataset = mnist::read_dataset_direct<std::vector, etl::dyn_matrix<float, 1>>(500);
    REQUIRE(!dataset.training_images.empty());

    mnist::binarize_dataset(dataset);

    auto dbn = std::make_unique<dbn_t>();

    dbn->template layer_get<0>().init_layer(28 * 28, 100);
    dbn->template layer_get<1>().init_layer(100, 200);
    dbn->template layer_get<2>().init_layer(200, 10);

    dbn->pretrain(dataset.training_images, 20);

    auto ft_error = dbn->fine_tune(dataset.training_images, dataset.training_labels, 100);
    std::cout << "ft_error:" << ft_error << std::endl;
    CHECK(ft_error < 5e-2);

    auto test_error = dll::test_set(dbn, dataset.test_images, dataset.test_labels, dll::predictor());
    std::cout << "test_error:" << test_error << std::endl;
    REQUIRE(test_error < 0.2);
}

TEST_CASE("dyn_dbn/cg/mnist/2", "dbn::memory") {
    typedef dll::dbn_desc<
        dll::dbn_layers<
            dll::dyn_rbm_desc<dll::momentum, dll::init_weights>::layer_t,
            dll::dyn_rbm_desc<dll::momentum>::layer_t,
            dll::dyn_rbm_desc<dll::momentum, dll::hidden<dll::unit_type::SOFTMAX>>::layer_t>,
        dll::batch_mode, dll::batch_size<50>, dll::big_batch_size<3>>::dbn_t dbn_t;

    auto dataset = mnist::read_dataset_direct<std::vector, etl::dyn_matrix<float, 1>>(1078);

    REQUIRE(!dataset.training_images.empty());

    mnist::binarize_dataset(dataset);

    auto dbn = std::make_unique<dbn_t>();

    dbn->template layer_get<0>().init_layer(28 * 28, 100);
    dbn->template layer_get<1>().init_layer(100, 200);
    dbn->template layer_get<2>().init_layer(200, 10);

    dbn->pretrain(dataset.training_images, 20);
    auto error = dbn->fine_tune(
        dataset.training_images.begin(), dataset.training_images.end(),
        dataset.training_labels.begin(), dataset.training_labels.end(),
        10);

    REQUIRE(error < 5e-2);

    auto test_error = dll::test_set(dbn, dataset.test_images, dataset.test_labels, dll::predictor());

    std::cout << "test_error:" << test_error << std::endl;

    REQUIRE(test_error < 0.2);

    //Mostly here to ensure compilation
    auto out = dbn->prepare_one_output<etl::dyn_matrix<float, 1>>();
    REQUIRE(out.size() > 0);
}
