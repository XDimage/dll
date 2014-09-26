//=======================================================================
// Copyright (c) 2014 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#ifndef DLL_SVM_COMMON
#define DLL_SVM_COMMON

//SVM Support is optional cause it requires libsvm

#ifdef DLL_SVM_SUPPORT

#include "nice_svm.hpp"

namespace dll {

inline svm_parameter default_svm_parameters(){
    auto parameters = svm::default_parameters();

    parameters.svm_type = C_SVC;
    parameters.kernel_type = RBF;
    parameters.probability = 1;
    parameters.C = 2.8;
    parameters.gamma = 0.0073;

    return parameters;
}

template<typename DBN>
void svm_store(const DBN& dbn, std::ostream& os){
    if(dbn.svm_loaded){
        binary_write(os, true);

        svm::save(dbn.svm_model, "..tmp.svm");

        std::ifstream svm_is("..tmp.svm", std::ios::binary);

        char buffer[1024];

        while(true){
            svm_is.read(buffer, 1024);

            if(svm_is.gcount() == 0){
                break;
            }

            os.write(buffer, svm_is.gcount());
        }
    } else {
        binary_write(os, false);
    }
}

template<typename DBN>
void svm_load(DBN& dbn, std::istream& is){
    dbn.svm_loaded = false;

    if(is.good()){
        bool svm;
        binary_load(is, svm);

        if(svm){
            std::ofstream svm_os("..tmp.svm", std::ios::binary);

            char buffer[1024];

            while(true){
                is.read(buffer, 1024);

                if(is.gcount() ==0){
                    break;
                }

                svm_os.write(buffer, is.gcount());
            }

            svm_os.close();

            dbn.svm_model = svm::load("..tmp.svm");

            dbn.svm_loaded = true;
        }
    }
}

template<typename DBN, typename Samples, typename Labels>
void make_problem(DBN& dbn, const Samples& training_data, const Labels& labels){
    auto n_samples = training_data.size();

    std::vector<etl::dyn_vector<double>> svm_samples;

    //Get all the activation probabilities
    for(std::size_t i = 0; i < n_samples; ++i){
        svm_samples.emplace_back(DBN::output_size());
        dbn.activation_probabilities(training_data[i], svm_samples[i]);
    }

    //static_cast ensure using the correct overload
    dbn.problem = svm::make_problem(labels, static_cast<const std::vector<etl::dyn_vector<double>>&>(svm_samples));
}

template<typename DBN, typename Samples, typename Labels>
bool svm_train(DBN& dbn, const Samples& training_data, const Labels& labels, const svm_parameter& parameters){
    make_problem(dbn, training_data, labels);

    //Make libsvm quiet
    svm::make_quiet();

    //Make sure parameters are not messed up
    if(!svm::check(dbn.problem, parameters)){
        return false;
    }

    //Train the SVM
    dbn.svm_model = svm::train(dbn.problem, parameters);

    dbn.svm_loaded = true;

    return true;
}

template<typename DBN, typename Samples, typename Labels>
bool svm_grid_search(DBN& dbn, const Samples& training_data, const Labels& labels, std::size_t n_fold = 5, const svm::rbf_grid& g = svm::rbf_grid()){
    make_problem(dbn, training_data, labels);

    //Make libsvm quiet
    svm::make_quiet();

    auto parameters = default_svm_parameters();

    //Make sure parameters are not messed up
    if(!svm::check(dbn.problem, parameters)){
        return false;
    }

    //Perform a grid-search
    svm::rbf_grid_search(dbn.problem, parameters, n_fold, g);

    return true;
}

} // end of namespace dll

#endif //DLL_SVM_SUPPORT

#endif //DLL_SVM_COMMON
