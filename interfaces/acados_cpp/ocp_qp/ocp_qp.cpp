
#include <algorithm>
#include <iostream>
#include <iterator>
#include <numeric>
#include <stdexcept>
#include <cstdlib>
#include <cmath>

#include "acados_cpp/ocp_qp/ocp_qp.hpp"

#include "acados/utils/print.h"
#include "acados_c/ocp_qp_interface.h"
#include "acados_c/options.h"

#include "acados_cpp/ocp_qp/hpipm_helper.hpp"
#include "acados_cpp/ocp_qp/ocp_qp_dimensions.hpp"
#include "acados_cpp/ocp_qp/utils.hpp"

using std::map;
using std::string;
using std::vector;

namespace acados {

ocp_qp::ocp_qp(std::vector<uint> nx, std::vector<uint> nu, std::vector<uint> nbx,
             std::vector<uint> nbu, std::vector<uint> ng, std::vector<uint> ns)
             : N(nx.size() - 1), qp(nullptr), solver(nullptr), needs_initializing(true) {


    // Number of controls on last stage should be zero;
    if (!nu.empty()) nu.back() = 0;
    if (!nbu.empty()) nbu.back() = 0;

    auto dim = create_ocp_qp_dimensions_ptr({
        {"nx", nx}, {"nu", nu}, {"nbx", nbx}, {"nbu", nbu}, {"ng", ng}, {"ns", ns}
    });

    qp = std::unique_ptr<ocp_qp_in>(ocp_qp_in_create(NULL, dim.get()));

    for (uint i = 0; i <= N; ++i) {
        std::vector<uint> idx_states(nbx.at(i));
        std::iota(std::begin(idx_states), std::end(idx_states), 0);
        set_bounds_indices("x", i, idx_states);

        std::vector<uint> idx_controls(nbu.at(i));
        std::iota(std::begin(idx_controls), std::end(idx_controls), 0);
        set_bounds_indices("u", i, idx_controls);
    }

    for (uint stage = 0; stage <= N; ++stage) {
        cached_bounds["lbx"].push_back(vector<double>(qp->dim->nx[stage], -INFINITY));
        cached_bounds["ubx"].push_back(vector<double>(qp->dim->nx[stage], +INFINITY));
        cached_bounds["lbu"].push_back(vector<double>(qp->dim->nu[stage], -INFINITY));
        cached_bounds["ubu"].push_back(vector<double>(qp->dim->nu[stage], +INFINITY));
    }

    // build map with available solvers, depending on how acados was compiled
    available_solvers = {
        {"condensing_hpipm", {FULL_CONDENSING_HPIPM}},
        {"sparse_hpipm", {PARTIAL_CONDENSING_HPIPM}},
#ifdef ACADOS_WITH_HPMPC
        {"hpmpc", {PARTIAL_CONDENSING_HPMPC}},
#endif
#ifdef ACADOS_WITH_OOQP
        {"ooqp", {PARTIAL_CONDENSING_OOQP}}
#endif
#ifdef ACADOS_WITH_QPDUNES
        {"qpdunes", {PARTIAL_CONDENSING_QPDUNES}},
#endif
#ifdef ACADOS_WITH_QPOASES
        {"qpoases", {FULL_CONDENSING_QPOASES}},
#endif
#ifdef ACADOS_WITH_QORE
        {"qore", {FULL_CONDENSING_QORE}},
#endif
    };
}

ocp_qp::ocp_qp(uint N, uint nx, uint nu, uint nbx, uint nbu, uint ng, uint ns)
    : ocp_qp(std::vector<uint>(N+1, nx), std::vector<uint>(N+1, nu), std::vector<uint>(N+1, nbx),
      std::vector<uint>(N+1, nbu), std::vector<uint>(N+1, ng), std::vector<uint>(N+1, ns)) {}

/*
 * Update all fields with the same values. Matrices are passed in column-major ordering.
 */
void ocp_qp::set_field(string field, vector<double> v) {
    uint last_stage = N;
    if (field == "A" || field == "B" || field == "b")
        last_stage = N-1;

    for (uint stage = 0; stage <= last_stage; ++stage)
        set_field(field, stage, v);
}

/*
 * Update one field with some values. Matrices are passed in column-major ordering.
 */
void ocp_qp::set_field(std::string field, uint stage, std::vector<double> v) {

    if (!in_range(field, stage))
        throw std::out_of_range("Stage index should be in [0, N].");
    if (!match(shape_of_field(field, stage), v.size()))
        throw std::invalid_argument("I need " + std::to_string(shape_of_field(field, stage)) +
                                    " elements but got " + std::to_string(v.size()) + ".");

    if (field == "lbx" || field == "ubx" || field == "lbu" || field == "ubu") {
        cached_bounds.at(field).at(stage) = v;
        auto idxb_stage = bounds_indices(string(1, field.back())).at(stage);
        vector<uint> sq_ids;
        if (field.front() == 'l') {
            string other(field);
            other.front() = 'u';
            sq_ids = idxb(v, cached_bounds.at(other).at(stage));
        }
        else if (field.front() == 'u') {
            string other(field);
            other.front() = 'l';
            sq_ids = idxb(cached_bounds.at(other).at(stage), v);
        }

        if (idxb_stage != sq_ids) {
            expand_dimensions();
            needs_initializing = true;
        }
    } else if (field == "Q") {
        d_cvt_colmaj_to_ocp_qp_Q(stage, v.data(), qp.get());
    } else if (field == "S") {
        d_cvt_colmaj_to_ocp_qp_S(stage, v.data(), qp.get());
    } else if (field == "R") {
        d_cvt_colmaj_to_ocp_qp_R(stage, v.data(), qp.get());
    } else if (field == "q") {
        d_cvt_colmaj_to_ocp_qp_q(stage, v.data(), qp.get());
    } else if (field == "r") {
        d_cvt_colmaj_to_ocp_qp_r(stage, v.data(), qp.get());
    } else if (field == "A") {
        d_cvt_colmaj_to_ocp_qp_A(stage, v.data(), qp.get());
    } else if (field == "B") {
        d_cvt_colmaj_to_ocp_qp_B(stage, v.data(), qp.get());
    } else if (field == "b") {
        d_cvt_colmaj_to_ocp_qp_b(stage, v.data(), qp.get());
    } else if (field == "C") {
        d_cvt_colmaj_to_ocp_qp_C(stage, v.data(), qp.get());
    } else if (field == "D") {
        d_cvt_colmaj_to_ocp_qp_D(stage, v.data(), qp.get());
    } else if (field == "lg") {
        d_cvt_colmaj_to_ocp_qp_lg(stage, v.data(), qp.get());
    } else if (field == "ug") {
        d_cvt_colmaj_to_ocp_qp_ug(stage, v.data(), qp.get());
    } else {
        throw std::invalid_argument("OCP QP does not contain field " + field);
    }
}

void ocp_qp::initialize_solver(string solver_name, map<string, option_t *> options) {

    // check if solver is available
    ocp_qp_solver_plan plan;
    try {
        plan = available_solvers.at(solver_name);
    } catch (std::exception e) {
        throw std::invalid_argument("QP solver '" + solver_name + "' is not available.");
    }

    squeeze_dimensions();

    config.reset(ocp_qp_config_create(plan));
    args.reset(ocp_qp_opts_create(config.get(), qp->dim));
    process_options(solver_name, options, args.get());

    solver.reset(ocp_qp_create(config.get(), qp->dim, args.get()));

    needs_initializing = false;
    cached_solver = solver_name;
}

vector<uint> ocp_qp::idxb(vector<double> lower_bound, vector<double> upper_bound) {
    vector<uint> bound_indices;
    if (lower_bound.size() != upper_bound.size())
        throw std::invalid_argument("Lower bound must have same shape as upper bound.");

    for (uint idx = 0; idx < lower_bound.size(); ++idx)
        if (lower_bound.at(idx) != -INFINITY || upper_bound.at(idx) != +INFINITY)
            bound_indices.push_back(idx); // there is a one-sided or two-sided bound at this index

    return bound_indices;
}

void ocp_qp::squeeze_dimensions() {
    map<string, vector<vector<uint>>> idxb_new;
    map<string, vector<uint>> nb;
    map<string, vector<vector<double>>> lb;
    map<string, vector<vector<double>>> ub;
    for (string bound : {"x", "u"}) {
        for (uint stage = 0; stage <= N; ++stage) {
            auto idxb_stage = idxb(cached_bounds.at("lb" + bound).at(stage), cached_bounds.at("ub" + bound).at(stage));
            idxb_new[bound].push_back(idxb_stage);
            nb[bound].push_back(idxb_new.at(bound).at(stage).size());
        }
    }

    d_change_bounds_dimensions_ocp_qp(reinterpret_cast<int *>(nb.at("u").data()),
                                      reinterpret_cast<int *>(nb.at("x").data()),
                                      qp.get());

    needs_initializing = true;

    for (string bound : {"x", "u"})
        for (uint stage = 0; stage <= N; ++stage)
            set_bounds_indices(bound, stage, idxb_new.at(bound).at(stage));
}

void ocp_qp::expand_dimensions() {

    map<string, vector<vector<double>>> lb;
    map<string, vector<vector<double>>> ub;

    for (string bound : {"x", "u"}) {
        for (uint stage = 0; stage <= N; ++stage) {
            vector<double> lb_old = get_field("lb" + bound).at(stage), ub_old = get_field("ub" + bound).at(stage);
            vector<uint> idxb = bounds_indices(bound).at(stage);
            vector<double> lb_new, ub_new;
            for (uint idx = 0, bound_index = 0; idx < dimensions()["n" + bound].at(stage); ++idx) {
                if (bound_index < dimensions()["nb" + bound].at(stage) && idx == idxb.at(bound_index)) {
                    lb_new.push_back(lb_old.at(bound_index));
                    ub_new.push_back(ub_old.at(bound_index));
                    ++bound_index;
                } else {
                    lb_new.push_back(-INFINITY);
                    ub_new.push_back(+INFINITY);
                }
            }
            lb[bound].push_back(lb_new);
            ub[bound].push_back(ub_new);
        }
    }

    d_change_bounds_dimensions_ocp_qp(qp->dim->nu, qp->dim->nx, qp.get());

    needs_initializing = true;

    for (uint i = 0; i <= N; ++i) {
        std::vector<uint> idx_states(dimensions()["nx"].at(i));
        std::iota(std::begin(idx_states), std::end(idx_states), 0);
        set_bounds_indices("x", i, idx_states);

        std::vector<uint> idx_controls(dimensions()["nu"].at(i));
        std::iota(std::begin(idx_controls), std::end(idx_controls), 0);
        set_bounds_indices("u", i, idx_controls);
    }
}

void ocp_qp::fill_in_bounds() {
    for (auto it : cached_bounds) {
        for (uint stage = 0; stage <= N; ++stage) {
            auto idxb_stage = bounds_indices(std::string(1, it.first.back())).at(stage);
            auto stage_bound = it.second.at(stage);
            vector<double> new_bound;
            // std::cout << "stage_bound: " << std::to_string(stage_bound);
            for (uint idx = 0; idx < idxb_stage.size(); ++idx) {
                if (it.first.front() == 'l')
                    new_bound.push_back(std::isfinite(stage_bound.at(idxb_stage.at(idx))) ? stage_bound.at(idxb_stage.at(idx)) : ACADOS_NEG_INFTY);
                else if (it.first.front() == 'u')
                    new_bound.push_back(std::isfinite(stage_bound.at(idxb_stage.at(idx))) ? stage_bound.at(idxb_stage.at(idx)) : ACADOS_POS_INFTY);
            }
            if (it.first == "lbx")
                d_cvt_colmaj_to_ocp_qp_lbx(stage, new_bound.data(), qp.get());
            else if (it.first == "ubx")
                d_cvt_colmaj_to_ocp_qp_ubx(stage, new_bound.data(), qp.get());
            else if (it.first == "lbu")
                d_cvt_colmaj_to_ocp_qp_lbu(stage, new_bound.data(), qp.get());
            else if (it.first == "ubu")
                d_cvt_colmaj_to_ocp_qp_ubu(stage, new_bound.data(), qp.get());
        }
    }
}

ocp_qp_solution ocp_qp::solve() {

    if (needs_initializing)
        throw std::runtime_error("Reinitialize solver");

    fill_in_bounds();

    auto result = std::unique_ptr<ocp_qp_out>(ocp_qp_out_create(NULL, qp->dim));

    int_t return_code = ocp_qp_solve(solver.get(), qp.get(), result.get());

    if (return_code != ACADOS_SUCCESS) {
        if (return_code == ACADOS_MAXITER)
            throw std::runtime_error("QP solver " + cached_solver + " reached maximum number of iterations.");
        else if (return_code == ACADOS_MINSTEP)
            throw std::runtime_error("QP solver " + cached_solver + " reached minimum step size.");
        else
            throw std::runtime_error("QP solver " + cached_solver + " failed with solver-specific error code " + std::to_string(return_code));
    }
    return ocp_qp_solution(std::move(result));
}

vector<vector<uint>> ocp_qp::bounds_indices(string name) {

    vector<vector<uint>> idxb;
    if (name == "x") {
        for (uint stage = 0; stage <= N; ++stage) {
            idxb.push_back(vector<uint>());
            for(int i = 0; i < qp->dim->nb[stage]; ++i)
                if (qp->idxb[stage][i] >= qp->dim->nu[stage])
                    idxb.at(stage).push_back(qp->idxb[stage][i] - qp->dim->nu[stage]);
        }
    } else if (name == "u") {
        for (uint stage = 0; stage <= N; ++stage) {
            idxb.push_back(vector<uint>());
            for(int i = 0; i < qp->dim->nb[stage]; ++i)
                if (qp->idxb[stage][i] < qp->dim->nu[stage])
                    idxb.at(stage).push_back(qp->idxb[stage][i]);
        }
    } else throw std::invalid_argument("Can only get bounds from x and u, you gave: '" + name + "'.");
    return idxb;
}

void ocp_qp::set_bounds_indices(string name, uint stage, vector<uint> v) {
    uint nb_bounds;
    if (name == "x")
        nb_bounds = qp->dim->nbx[stage];
    else if (name == "u")
        nb_bounds = qp->dim->nbu[stage];
    else
        throw std::invalid_argument("Can only set bounds on x and u, you gave: '" + name + "'.");

    if (nb_bounds != v.size())
        throw std::invalid_argument("I need " + std::to_string(nb_bounds) + " indices, you gave " + std::to_string(v.size()) + ".");
    for (uint i = 0; i < nb_bounds; ++i)
        if (name == "x")
            qp->idxb[stage][qp->dim->nbu[stage]+i] = qp->dim->nu[stage]+v.at(i);
        else if (name == "u")
            qp->idxb[stage][i] = v.at(i);
}

map<string, std::function<void(int, ocp_qp_in *, double *)>> ocp_qp::extract_functions = {
        {"Q", d_cvt_ocp_qp_to_colmaj_Q},
        {"S", d_cvt_ocp_qp_to_colmaj_S},
        {"R", d_cvt_ocp_qp_to_colmaj_R},
        {"q", d_cvt_ocp_qp_to_colmaj_q},
        {"r", d_cvt_ocp_qp_to_colmaj_r},
        {"A", d_cvt_ocp_qp_to_colmaj_A},
        {"B", d_cvt_ocp_qp_to_colmaj_B},
        {"b", d_cvt_ocp_qp_to_colmaj_b},
        {"lbx", d_cvt_ocp_qp_to_colmaj_lbx},
        {"ubx", d_cvt_ocp_qp_to_colmaj_ubx},
        {"lbu", d_cvt_ocp_qp_to_colmaj_lbu},
        {"ubu", d_cvt_ocp_qp_to_colmaj_ubu},
        {"C", d_cvt_ocp_qp_to_colmaj_C},
        {"D", d_cvt_ocp_qp_to_colmaj_D},
        {"lg", d_cvt_ocp_qp_to_colmaj_lg},
        {"ug", d_cvt_ocp_qp_to_colmaj_ug}
    };


vector< vector<double> > ocp_qp::get_field(std::string field) {
    uint last_index = N;
    if (field == "A" || field == "B" || field == "b")
        last_index = N-1;
    vector< vector<double> > result;
    for (uint i = 0; i <= last_index; i++) {
        auto dims = shape_of_field(field, i);
        vector<double> v(dims.first * dims.second);
        extract_functions[field](i, qp.get(), v.data());
        result.push_back(v);
    }
    return result;
}


map<string, vector<uint>> ocp_qp::dimensions() {
    return {{"nx", nx()}, {"nu", nu()}, {"nbx", nbx()}, {"nbu", nbu()}, {"ng", ng()}};
}


std::vector<uint> ocp_qp::nx() {
    std::vector<uint> tmp(N+1);
    std::copy_n(qp->dim->nx, N+1, tmp.begin());
    return tmp;
}

std::vector<uint> ocp_qp::nu() {
    std::vector<uint> tmp(N+1);
    std::copy_n(qp->dim->nu, N+1, tmp.begin());
    return tmp;
}

std::vector<uint> ocp_qp::nbx() {
    std::vector<uint> tmp(N+1);
    std::copy_n(qp->dim->nbx, N+1, tmp.begin());
    return tmp;
}

std::vector<uint> ocp_qp::nbu() {
    std::vector<uint> tmp(N+1);
    std::copy_n(qp->dim->nbu, N+1, tmp.begin());
    return tmp;
}

std::vector<uint> ocp_qp::ng() {
    std::vector<uint> tmp(N+1);
    std::copy_n(qp->dim->ng, N+1, tmp.begin());
    return tmp;
}

bool ocp_qp::in_range(std::string field, uint stage) {
    return (field == "A" || field == "B" || field == "b") ? (stage < N) : (stage <= N);
}

std::pair<uint, uint> ocp_qp::shape_of_field(std::string field, uint stage) {

    if(!in_range(field, stage))
        throw std::out_of_range("Stage index should be in [0, N].");

    if (field == "Q")
        return std::make_pair(num_rows_Q(stage, qp->dim), num_cols_Q(stage, qp->dim));
    else if (field == "S")
        return std::make_pair(num_rows_S(stage, qp->dim), num_cols_S(stage, qp->dim));
    else if (field == "R")
        return std::make_pair(num_rows_R(stage, qp->dim), num_cols_R(stage, qp->dim));
    else if (field == "q")
        return std::make_pair(num_elems_q(stage, qp->dim), 1);
    else if (field == "r")
        return std::make_pair(num_elems_r(stage, qp->dim), 1);
    else if (field == "A")
        return std::make_pair(num_rows_A(stage, qp->dim), num_cols_A(stage, qp->dim));
    else if (field == "B")
        return std::make_pair(num_rows_B(stage, qp->dim), num_cols_B(stage, qp->dim));
    else if (field == "b")
        return std::make_pair(num_elems_b(stage, qp->dim), 1);
    else if (field == "lbx")
        return std::make_pair(qp->dim->nx[stage], 1);
    else if (field == "ubx")
        return std::make_pair(qp->dim->nx[stage], 1);
    else if (field == "lbu")
        return std::make_pair(qp->dim->nu[stage], 1);
    else if (field == "ubu")
        return std::make_pair(qp->dim->nu[stage], 1);
    else if (field == "C")
        return std::make_pair(num_rows_C(stage, qp->dim), num_cols_C(stage, qp->dim));
    else if (field == "D")
        return std::make_pair(num_rows_D(stage, qp->dim), num_cols_D(stage, qp->dim));
    else if (field == "lg")
        return std::make_pair(num_elems_lg(stage, qp->dim), 1);
    else if (field == "ug")
        return std::make_pair(num_elems_ug(stage, qp->dim), 1);
    else
        throw std::invalid_argument("OCP QP does not contain field " + field);
}

}  // namespace acados