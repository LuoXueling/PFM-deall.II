//
// Created by xlluo on 24-7-30.
//

#ifndef CRACKS_POST_PROCESSORS_H
#define CRACKS_POST_PROCESSORS_H

#include "constitutive_law.h"
#include "dealii_includes.h"
#include "utils.h"

template <int dim>
class StrainPostprocessor : public DataPostprocessorTensor<dim> {
public:
  StrainPostprocessor()
      : DataPostprocessorTensor<dim>("Strain", update_gradients) {}

  void evaluate_vector_field(
      const DataPostprocessorInputs::Vector<dim> &input_data,
      std::vector<Vector<double>> &computed_quantities) const override {
    AssertDimension(input_data.solution_gradients.size(),
                    computed_quantities.size());

    for (unsigned int p = 0; p < input_data.solution_gradients.size(); ++p) {
      AssertDimension(computed_quantities[p].size(),
                      (Tensor<2, dim>::n_independent_components));
      for (unsigned int d = 0; d < dim; ++d)
        for (unsigned int e = 0; e < dim; ++e)
          computed_quantities[p][Tensor<2, dim>::component_to_unrolled_index(
              TableIndices<2>(d, e))] =
              (input_data.solution_gradients[p][d][e] +
               input_data.solution_gradients[p][e][d]) /
              2;
    }
  }
};

template <int dim>
class StressPostprocessor : public DataPostprocessorTensor<dim> {
public:
  ConstitutiveLaw<dim> constitutive_law;

  StressPostprocessor(ConstitutiveLaw<dim> &constitutive_law_input)
      : DataPostprocessorTensor<dim>("Stress", update_gradients),
        constitutive_law(constitutive_law_input) {}

  void evaluate_vector_field(
      const DataPostprocessorInputs::Vector<dim> &input_data,
      std::vector<Vector<double>> &computed_quantities) const override {
    AssertDimension(input_data.solution_gradients.size(),
                    computed_quantities.size());

    for (unsigned int p = 0; p < input_data.solution_gradients.size(); ++p) {
      AssertDimension(computed_quantities[p].size(),
                      (Tensor<2, dim>::n_independent_components));

      Tensor<2, dim> grad_u;
      grad_u[0] = input_data.solution_gradients[p][0];
      grad_u[1] = input_data.solution_gradients[p][1];
      const Tensor<2, dim> Identity = Tensors ::get_Identity<dim>();
      const Tensor<2, dim> E = 0.5 * (grad_u + transpose(grad_u));
      const double tr_E = trace(E);
      Tensor<2, dim> stress = constitutive_law.lambda * tr_E * Identity +
                              2 * constitutive_law.mu * E;

      for (unsigned int d = 0; d < dim; ++d)
        for (unsigned int e = 0; e < dim; ++e) {
          computed_quantities[p][Tensor<2, dim>::component_to_unrolled_index(
              TableIndices<2>(d, e))] = stress[d][e];
        }
    }
  }
};

template <int dim> class CellProcessor {
public:
  CellProcessor(FESystem<dim> &fe, Controller<dim> &ctl);

  Vector<double> evaluate_scalar(LA::MPI::Vector &solution,
                                 DoFHandler<dim> &dof_handler,
                                 Controller<dim> &);
  std::vector<Vector<double>> evaluate_vector(LA::MPI::Vector &solution,
                                              DoFHandler<dim> &dof_handler,
                                              Controller<dim> &);
  void add_data_scalar(LA::MPI::Vector &solution, DataOut<dim> &,
                       DoFHandler<dim> &dof_handler, Controller<dim> &);
  void add_data_vector(LA::MPI::Vector &solution, DataOut<dim> &,
                       DoFHandler<dim> &dof_handler, Controller<dim> &);
  virtual unsigned int get_n_components() {
    AssertThrow(false, ExcNotImplemented());
  };
  virtual void
  get_q_scalar_values(Vector<double> &,
                      const std::vector<std::shared_ptr<PointHistory>> &,
                      LA::MPI::Vector &, Controller<dim> &) {
    AssertThrow(false, ExcNotImplemented());
  };
  virtual void
  get_q_vector_values(std::vector<Vector<double>> &,
                      const std::vector<std::shared_ptr<PointHistory>> &,
                      LA::MPI::Vector &, Controller<dim> &) {
    AssertThrow(false, ExcNotImplemented());
  };
  virtual std::string get_name() { AssertThrow(false, ExcNotImplemented()); };

  FEValues<dim> fe_values;
};

template <int dim>
CellProcessor<dim>::CellProcessor(FESystem<dim> &fe, Controller<dim> &ctl)
    : fe_values(fe, ctl.quadrature_formula,
                update_values | update_gradients | update_quadrature_points |
                    update_JxW_values) {}

template <int dim>
Vector<double> CellProcessor<dim>::evaluate_scalar(LA::MPI::Vector &solution,
                                                   DoFHandler<dim> &dof_handler,
                                                   Controller<dim> &ctl) {
  Vector<double> res(ctl.triangulation.n_active_cells());
  Vector<double> q_res(ctl.quadrature_formula.size());
  for (auto &cell : dof_handler.active_cell_iterators())
    if (cell->is_locally_owned()) {
      fe_values.reinit(cell);
      q_res = 0;
      const std::vector<std::shared_ptr<PointHistory>> lqph =
          ctl.quadrature_point_history.get_data(cell);
      get_q_scalar_values(q_res, lqph, solution, ctl);
      res(cell->active_cell_index()) = q_res.mean_value();
    }
  return res;
};

template <int dim>
std::vector<Vector<double>>
CellProcessor<dim>::evaluate_vector(LA::MPI::Vector &solution,
                                    DoFHandler<dim> &dof_handler,
                                    Controller<dim> &ctl) {
  std::vector<Vector<double>> res(get_n_components());
  for (unsigned int j = 0; j < get_n_components(); ++j) {
    res[j].reinit(ctl.triangulation.n_active_cells());
  }
  std::vector<Vector<double>> q_res(ctl.quadrature_formula.size());
  for (unsigned int j = 0; j < ctl.quadrature_formula.size(); ++j) {
    q_res[j].reinit(get_n_components());
  }
  for (auto &cell : dof_handler.active_cell_iterators())
    if (cell->is_locally_owned()) {
      fe_values.reinit(cell);
      const std::vector<std::shared_ptr<PointHistory>> lqph =
          ctl.quadrature_point_history.get_data(cell);
      for (unsigned int j = 0; j < ctl.quadrature_formula.size(); ++j) {
        q_res[j].reinit(get_n_components());
      }
      get_q_vector_values(q_res, lqph, solution, ctl);
      for (unsigned int i_comp = 0; i_comp < get_n_components(); ++i_comp) {
        for (unsigned int q = 0; q < ctl.quadrature_formula.size(); ++q) {
          res[i_comp](cell->active_cell_index()) +=
              q_res[q][i_comp] / ctl.quadrature_formula.size();
        }
      }
    }
  return res;
}

template <int dim>
void CellProcessor<dim>::add_data_scalar(LA::MPI::Vector &solution,
                                         DataOut<dim> &data_out,
                                         DoFHandler<dim> &dof_handler,
                                         Controller<dim> &ctl) {
  Vector<double> data = evaluate_scalar(solution, dof_handler, ctl);
  data_out.add_data_vector(data, get_name());
}

template <int dim>
void CellProcessor<dim>::add_data_vector(LA::MPI::Vector &solution,
                                         DataOut<dim> &data_out,
                                         DoFHandler<dim> &dof_handler,
                                         Controller<dim> &ctl) {
  std::vector<Vector<double>> data =
      evaluate_vector(solution, dof_handler, ctl);
  for (unsigned int i = 0; i < get_n_components(); ++i) {
    data_out.add_data_vector(data[i], get_name() + "_" + std::to_string(i + 1));
  }
}

std::string replaceAll(std::string &s_in, const std::string &search,
                       const std::string &replace) {
  std::string s = s_in;
  for (size_t pos = 0;; pos += replace.length()) {
    // Locate the substring to replace
    pos = s.find(search, pos);
    if (pos == std::string::npos)
      break;
    // Replace by erasing and inserting
    s.erase(pos, search.length());
    s.insert(pos, replace);
  }
  return s;
}

template <int dim> class PointHistoryProcessor : public CellProcessor<dim> {
public:
  PointHistoryProcessor(std::string name_in, FESystem<dim> &fe_in,
                        Controller<dim> &);

  std::string name;

  void get_q_scalar_values(Vector<double> &,
                           const std::vector<std::shared_ptr<PointHistory>> &,
                           LA::MPI::Vector &solution,
                           Controller<dim> &) override;
  std::string get_name() override { return replaceAll(name, " ", "_"); };
};

template <int dim>
PointHistoryProcessor<dim>::PointHistoryProcessor(std::string name_in,
                                                  FESystem<dim> &fe_in,
                                                  Controller<dim> &ctl_in)
    : CellProcessor<dim>(fe_in, ctl_in), name(name_in){};

template <int dim>
void PointHistoryProcessor<dim>::get_q_scalar_values(
    Vector<double> &data,
    const std::vector<std::shared_ptr<PointHistory>> &lqph,
    LA::MPI::Vector & /*solution*/, Controller<dim> &ctl) {
  for (unsigned int q = 0; q < ctl.quadrature_formula.size(); ++q) {
    data[q] = lqph[q]->get(name, 0.0);
  }
}

#endif // CRACKS_POST_PROCESSORS_H
