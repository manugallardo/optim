/*################################################################################
  ##
  ##   Copyright (C) 2016-2022 Keith O'Hara
  ##
  ##   This file is part of the OptimLib C++ library.
  ##
  ##   Licensed under the Apache License, Version 2.0 (the "License");
  ##   you may not use this file except in compliance with the License.
  ##   You may obtain a copy of the License at
  ##
  ##       http://www.apache.org/licenses/LICENSE-2.0
  ##
  ##   Unless required by applicable law or agreed to in writing, software
  ##   distributed under the License is distributed on an "AS IS" BASIS,
  ##   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  ##   See the License for the specific language governing permissions and
  ##   limitations under the License.
  ##
  ################################################################################*/

/*
 * Gradient Descent (GD)
 */

#include "optim.hpp"

// [OPTIM_BEGIN]
optimlib_inline
bool
optim::internal::gd_basic_impl(
    Vec_t& init_out_vals, 
    std::function<double (const Vec_t& vals_inp, Vec_t* grad_out, void* opt_data)> opt_objfn, 
    void* opt_data, 
    algo_settings_t* settings_inp)
{
    // notation: 'p' stands for '+1'.

    bool success = false;
    
    const size_t n_vals = BMO_MATOPS_SIZE(init_out_vals);

    //
    // GD settings

    algo_settings_t settings;

    if (settings_inp) {
        settings = *settings_inp;
    }

    const int print_level = settings.print_level;
    
    const uint_t conv_failure_switch = settings.conv_failure_switch;
    const size_t iter_max = settings.iter_max;
    const double grad_err_tol = settings.grad_err_tol;
    const double rel_sol_change_tol = settings.rel_sol_change_tol;

    gd_settings_t gd_settings = settings.gd_settings;

    const bool vals_bound = settings.vals_bound;
    
    const Vec_t lower_bounds = settings.lower_bounds;
    const Vec_t upper_bounds = settings.upper_bounds;

    const VecInt_t bounds_type = determine_bounds_type(vals_bound, n_vals, lower_bounds, upper_bounds);

    // lambda function for box constraints

    std::function<double (const Vec_t& vals_inp, Vec_t* grad_out, void* box_data)> box_objfn \
    = [opt_objfn, vals_bound, bounds_type, lower_bounds, upper_bounds] (const Vec_t& vals_inp, Vec_t* grad_out, void* opt_data) \
    -> double 
    {
        if (vals_bound) {
            Vec_t vals_inv_trans = inv_transform(vals_inp, bounds_type, lower_bounds, upper_bounds);
            
            double ret;
            
            if (grad_out) {
                Vec_t grad_obj = *grad_out;

                ret = opt_objfn(vals_inv_trans, &grad_obj, opt_data);

                // Mat_t jacob_matrix = jacobian_adjust(vals_inp,bounds_type,lower_bounds,upper_bounds);
                Vec_t jacob_vec = BMO_MATOPS_EXTRACT_DIAG( jacobian_adjust(vals_inp,bounds_type,lower_bounds,upper_bounds) );

                // *grad_out = jacob_matrix * grad_obj; //
                *grad_out = BMO_MATOPS_HADAMARD_PROD(jacob_vec, grad_obj);
            } else {
                ret = opt_objfn(vals_inv_trans, nullptr, opt_data);
            }

            return ret;
        } else {
            return opt_objfn(vals_inp,grad_out,opt_data);
        }
    };

    //
    // initialization

    if (! BMO_MATOPS_IS_FINITE(init_out_vals) ) {
        printf("gd error: non-finite initial value(s).\n");
        return false;
    }

    Vec_t x = init_out_vals;
    Vec_t d = BMO_MATOPS_ZERO_VEC(n_vals);

    Vec_t adam_vec_m;
    Vec_t adam_vec_v;

    if (settings.gd_settings.method == 3 || settings.gd_settings.method == 4) {
        adam_vec_v = BMO_MATOPS_ZERO_VEC(n_vals);
    }

    if (settings.gd_settings.method == 5 || settings.gd_settings.method == 6 || settings.gd_settings.method == 7) {
        adam_vec_m = BMO_MATOPS_ZERO_VEC(n_vals);
        adam_vec_v = BMO_MATOPS_ZERO_VEC(n_vals);
    }

    if (vals_bound) { // should we transform the parameters?
        x = transform(x, bounds_type, lower_bounds, upper_bounds);
    }

    Vec_t grad(n_vals); // gradient
    box_objfn(x,&grad,opt_data);

    double grad_err = BMO_MATOPS_L2NORM(grad);

    OPTIM_GD_TRACE(-1, grad_err, 0.0, x, d, grad, adam_vec_m, adam_vec_v);

    if (grad_err <= grad_err_tol) {
        return true;
    }

    //
    // begin loop

    Vec_t grad_p = grad;
    double rel_sol_change = 1.0;

    size_t iter = 0;

    while (grad_err > grad_err_tol && rel_sol_change > rel_sol_change_tol && iter < iter_max) {
        ++iter;

        //

        Vec_t d_p = gd_update(x, grad, grad_p, d, box_objfn, opt_data, iter,
                              gd_settings, adam_vec_m, adam_vec_v);

        Vec_t x_p = x - d_p;
        grad = grad_p;

        box_objfn(x_p, &grad_p, opt_data);

        if (gd_settings.clip_grad) {
            gradient_clipping(grad_p, gd_settings);
        }

        //

        grad_err = BMO_MATOPS_L2NORM(grad_p);
        rel_sol_change = BMO_MATOPS_L1NORM( BMO_MATOPS_ARRAY_DIV_ARRAY((x_p - x), (BMO_MATOPS_ARRAY_ADD_SCALAR(BMO_MATOPS_ABS(x), 1.0e-08)) ) );

        d = d_p;
        x = x_p;

        //

        OPTIM_GD_TRACE(iter-1, grad_err, rel_sol_change, x, d, grad_p, adam_vec_m, adam_vec_v)
    }

    //

    if (vals_bound) {
        x = inv_transform(x, bounds_type, lower_bounds, upper_bounds);
    }

    error_reporting(init_out_vals, x, opt_objfn, opt_data, 
                    success, grad_err, grad_err_tol, iter, iter_max, 
                    conv_failure_switch, settings_inp);

    //

    return success;
}

optimlib_inline
bool
optim::gd(Vec_t& init_out_vals, 
          std::function<double (const Vec_t& vals_inp, Vec_t* grad_out, void* opt_data)> opt_objfn, 
          void* opt_data)
{
    return internal::gd_basic_impl(init_out_vals,opt_objfn,opt_data,nullptr);
}

optimlib_inline
bool
optim::gd(Vec_t& init_out_vals, 
          std::function<double (const Vec_t& vals_inp, Vec_t* grad_out, void* opt_data)> opt_objfn, 
          void* opt_data, 
          algo_settings_t& settings)
{
    return internal::gd_basic_impl(init_out_vals,opt_objfn,opt_data,&settings);
}
