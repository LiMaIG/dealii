/* $Id$ */
/* Copyright W. Bangerth, University of Heidelberg, 1998 */



#include <base/function.h>
#include <dofs/dof_handler.h>
#include <dofs/dof_accessor.h>
#include <grid/tria_iterator.h>
#include <dofs/dof_constraints.h>
#include <fe/fe.h>
#include <fe/fe_values.h>
#include <base/quadrature.h>
#include <numerics/assembler.h>
#include <numerics/vectors.h>
#include <numerics/matrices.h>
#include <dofs/dof_tools.h>
#include <lac/vector.h>
#include <lac/sparse_matrix.h>
#include <lac/precondition.h>
#include <lac/solver_cg.h>
#include <lac/vector_memory.h>

#include <numeric>
#include <algorithm>
#include <bvector.h>
#include <cmath>




inline double sqr (const double x) {
  return x*x;
};


template <int dim>
inline double sqr_point (const Tensor<1,dim> &p) {
  return p * p;
};




template <int dim>
void VectorTools::interpolate (const DoFHandler<dim> &dof,
			       const Function<dim>   &function,
			       Vector<double>        &vec)
{
  Assert (dof.get_fe().n_components() == function.n_components,
	  ExcComponentMismatch());
  
  const FiniteElement<dim> &fe           = dof.get_fe();
  const unsigned int        n_components = fe.n_components();
  const bool                fe_is_system = (n_components != 1);
  
  DoFHandler<dim>::active_cell_iterator cell = dof.begin_active(),
					endc = dof.end();

				   // For FESystems many of the
				   // unit_support_points will
				   // appear multiply, as a point
				   // may be unit_support_point
				   // for several of the components
				   // of the system.
				   // The following is rather
				   // complicated as it is
				   // avoided to evaluate
				   // the vectorfunction multiply at
				   // the same point on a cell.
  vector<Point<dim> > unit_support_points (fe.dofs_per_cell);
  fe.get_unit_support_points(unit_support_points);

				   // The following works well
				   // if #dofs_per_x<=1 (x=vertex,line,cell)#
				   // as then
				   // the multiple support_points
				   // are placed one after another.

				   // find the support points 
				   // on a cell that
				   // are multiply mentioned in 
				   // #unit_support_points#.
				   // Mark the first representative
				   // of each multiply mentioned
				   // support point by setting
				   // #true# in the boolean vector 
				   // #is_representative_point#.
//   vector<bool>  is_representative_point(fe.dofs_per_cell, false);
//   is_representative_point[0]=true;
//   unsigned int n_rep_points=1;
//   for (unsigned int last_rep_point=0, i=1; i<fe.dofs_per_cell; ++i)
//     {
//       if (unit_support_points[i] != unit_support_points[last_rep_point])
// 	{
// 	  is_representative_point[i] = true;
// 	  last_rep_point=i;
// 	  ++n_rep_points;
// 	}
//    };

//   vector<int>         dofs_on_cell (fe.dofs_per_cell);
//   vector<Point<dim> > support_points (fe.dofs_per_cell);

//   vector<Point<dim> > rep_points (n_rep_points);
//   vector<Vector<double> > function_values_at_rep_points (
//     n_rep_points, Vector<double>(fe.n_components()));

//   for (; cell!=endc; ++cell)
//     {
// 				       // for each cell:
// 				       // get location of finite element
// 				       // off-points (support_points)
//       fe.get_support_points (cell, support_points);

// 				       // pick out the representative
// 				       // support points
//       unsigned int j=0;
//       for (unsigned int i=0; i<fe.dofs_per_cell; ++i)
// 	if (is_representative_point[i])
// 	  rep_points[j++]=support_points[i];
//       Assert(j == n_rep_points, ExcInternalError());

// 				       // get function values at these points
//       vectorfunction.value_list (rep_points, function_values_at_rep_points);
  
// 					     // get indices of the dofs on this cell
//       cell->get_dof_indices (dofs_on_cell);

// 				       // distribute function values to the
// 				       // whole vector
//       int last_rep_point = -1;
// 				       // it holds `is_representative_point[0]=true'
// 				       // therefore the first #last_rep_point# is 0
// 				       // and we need to start with
// 				       // `last_rep_point = -1'
//       for (unsigned int i=0; i<fe.dofs_per_cell; ++i)
// 	{
// 	  if (is_representative_point[i])
// 	    ++last_rep_point;

// 	  const unsigned int component
// 	    = fe.system_to_component_index(i).first;
// 	  vec(dofs_on_cell[i])
// 	    = function_values_at_rep_points[last_rep_point](component);
// 	} 
//     }

				   // The following is more general.
				   // It also works if #dofs_per_x>1#,
				   // i.e. it is usable also for systems
				   // including
				   // FEQ3, FEQ4, FEDG_Qx.

				   // Find the support points 
				   // on a cell that
				   // are multiply mentioned in 
				   // #unit_support_points#.
				   // Mark the first representative
				   // of each multiply mentioned
				   // support point by appending its
				   // dof index to #dofs_of_rep_points#.
				   // Each multiple point gets to know
				   // the dof index of its representative
				   // point by the #dof_to_rep_dof_table#.

				   // the following vector collects all dofs i,
				   // 0<=i<fe.dofs_per_cell, for that
				   // unit_support_points[i] 
				   // is a representative one. i.e.
				   // the following vector collects all rep dofs.
				   // the position of a rep dof within this vector
				   // is called rep index.
  vector<unsigned int> dofs_of_rep_points;
				   // the following table converts a dof i
				   // to the rep index.
  vector<unsigned int> dof_to_rep_index_table;
  unsigned int n_rep_points=0;
  for (unsigned int i=0; i<fe.dofs_per_cell; ++i)
    {
      bool representative=true;
				       // the following loop is looped
				       // the other way round to get
				       // the minimal effort of
				       // O(fe.dofs_per_cell) for multiple
				       // support points that are placed
				       // one after the other.
      for (unsigned int j=dofs_of_rep_points.size(); j>0; --j)
	if (unit_support_points[i] 
	    == unit_support_points[dofs_of_rep_points[j-1]])
	  {
	    dof_to_rep_index_table.push_back(j-1);
	    representative=false;
	    break;
	  }
      
      if (representative)
	{
					   // rep_index=dofs_of_rep_points.size()
	  dof_to_rep_index_table.push_back(dofs_of_rep_points.size());
					   // dofs_of_rep_points[rep_index]=i
	  dofs_of_rep_points.push_back(i);
	  ++n_rep_points;
	}
    }
  Assert(dofs_of_rep_points.size()==n_rep_points, ExcInternalError());
  Assert(dof_to_rep_index_table.size()==fe.dofs_per_cell, ExcInternalError());

  vector<int>         dofs_on_cell (fe.dofs_per_cell);
  vector<Point<dim> > support_points (fe.dofs_per_cell);

  vector<Point<dim> > rep_points (n_rep_points);

				   // get space for the values of the
				   // function at the rep support points.
				   //
				   // have two versions, one for system fe
				   // and one for scalar ones, to take the
				   // more efficient one respectively
  vector<double>          function_values_scalar (n_rep_points);
  vector<Vector<double> > function_values_system (n_rep_points,
						  Vector<double>(fe.n_components()));

  for (; cell!=endc; ++cell)
    {
				       // for each cell:
				       // get location of finite element
				       // off-points (support_points)
      fe.get_support_points (cell, support_points);
      
				       // pick out the representative
				       // support points
      for (unsigned int j=0; j<dofs_of_rep_points.size(); ++j)
	rep_points[j]=support_points[dofs_of_rep_points[j]];

				       // get indices of the dofs on this cell
      cell->get_dof_indices (dofs_on_cell);


				       
      if (fe_is_system)
	{
					   // get function values at
					   // these points. Here: get
					   // all components
	  function.vector_value_list (rep_points, function_values_system);
					   // distribute the function
					   // values to the global
					   // vector
	  for (unsigned int i=0; i<fe.dofs_per_cell; ++i)
	    {
	      const unsigned int component
		= fe.system_to_component_index(i).first;
	      const unsigned int rep_dof=dof_to_rep_index_table[i];
	      vec(dofs_on_cell[i])
		= function_values_system[rep_dof](component);
	    };
	}
      
      else
	{
					   // get first component only,
					   // which is the only component
					   // in the function anyway
	  function.value_list (rep_points, function_values_scalar, 0);
					   // distribute the function
					   // values to the global
					   // vector
	  for (unsigned int i=0; i<fe.dofs_per_cell; ++i)
	    vec(dofs_on_cell[i]) 
	      = function_values_scalar[dof_to_rep_index_table[i]];
	};
    }
}



template <int dim> void
VectorTools::interpolate(const DoFHandler<dim>           &high_dof,
			 const DoFHandler<dim>           &low_dof,
			 const FullMatrix<double>        &transfer,
			 const Vector<double>            &high,
			 Vector<double>                  &low)
{
  Vector<double> cell_high(high_dof.get_fe().dofs_per_cell);
  Vector<double> cell_low(low_dof.get_fe().dofs_per_cell);
  
  DoFHandler<dim>::active_cell_iterator h = high_dof.begin_active();
  DoFHandler<dim>::active_cell_iterator l = low_dof.begin_active();
  const DoFHandler<dim>::cell_iterator endh = high_dof.end();
  
  for(; h != endh; ++h, ++l)
  {
    h->get_dof_values(high, cell_high);
    transfer.vmult(cell_low, cell_high);
    l->distribute_local_to_global(cell_low, low);
  }
}



#if deal_II_dimension == 1

template <>
void VectorTools::project (const DoFHandler<1>    &,
			   const ConstraintMatrix &,
			   const Quadrature<1>    &,
			   const Function<1>      &,
			   Vector<double>         &,
			   const bool              ,
			   const Quadrature<0>    &,
			   const bool              ) {
				   // this function should easily be implemented
				   // using the template below. However some
				   // changes have to be made since faces don't
				   // exist in 1D. Maybe integrate the creation of
				   // zero boundary values into the
				   // project_boundary_values function?
  Assert (false, ExcNotImplemented());
};

#endif




template <int dim>
void VectorTools::project (const DoFHandler<dim>    &dof,
			   const ConstraintMatrix   &constraints,
			   const Quadrature<dim>    &quadrature,
			   const Function<dim>      &function,
			   Vector<double>           &vec,
			   const bool                enforce_zero_boundary,
			   const Quadrature<dim-1>  &q_boundary,
			   const bool                project_to_boundary_first)
{
  Assert (dof.get_fe().n_components() == function.n_components,
	  ExcInvalidFE());
  
  const FiniteElement<dim> &fe = dof.get_fe();

				   // make up boundary values
  map<int,double> boundary_values;

  if (enforce_zero_boundary == true) 
				     // no need to project boundary values, but
				     // enforce homogeneous boundary values
				     // anyway
    {
      DoFHandler<dim>::active_face_iterator face = dof.begin_active_face(),
					    endf = dof.end_face();
      vector<int> face_dof_indices (fe.dofs_per_face);
      for (; face!=endf; ++face)
	if (face->at_boundary())
	  {
	    face->get_dof_indices (face_dof_indices);
	    for (unsigned int i=0; i<fe.dofs_per_face; ++i)
					       // enter zero boundary values
					       // for all boundary nodes
					       //
					       // we need not care about
					       // vector valued elements here,
					       // since we set all components
	      boundary_values[face_dof_indices[i]] = 0.;
	  };
    }
  else
				     // no homogeneous boundary values
    if (project_to_boundary_first == true)
				       // boundary projection required
      {
					 // set up a list of boundary functions for
					 // the different boundary parts. We want the
					 // #function# to hold on all parts of the
					 // boundary
	map<unsigned char,const Function<dim>*> boundary_functions;
	for (unsigned char c=0; c<255; ++c)
	  boundary_functions[c] = &function;
	project_boundary_values (dof, boundary_functions, q_boundary,
				 boundary_values);
      };

  
      
				   // set up mass matrix and right hand side
  vec.reinit (dof.n_dofs());
  SparseMatrixStruct sparsity(dof.n_dofs(),
			      dof.n_dofs(),
			      dof.max_couplings_between_dofs());
  DoFTools::make_sparsity_pattern (dof, sparsity);
  constraints.condense (sparsity);
  
  SparseMatrix<double> mass_matrix (sparsity);
  Vector<double> tmp (mass_matrix.n());

				   // try to assemble the mass matrix by exact
				   // integration. if this is not supported,
				   // then use quadrature
  try 
    {
      MatrixCreator<dim>::create_mass_matrix (dof, mass_matrix);
    }
  catch (FiniteElement<dim>::ExcComputationNotUseful)
    {
      MatrixCreator<dim>::create_mass_matrix (dof, quadrature, mass_matrix);
    };
  
  VectorTools::create_right_hand_side (dof, quadrature, function, tmp);

  constraints.condense (mass_matrix);
  constraints.condense (tmp);
  if (boundary_values.size() != 0)
    MatrixTools<dim>::apply_boundary_values (boundary_values,
					     mass_matrix, vec, tmp);

  SolverControl           control(1000,1e-16);
  PrimitiveVectorMemory<> memory;
  SolverCG<>              cg(control,memory);

  PreconditionRelaxation<>
    prec(mass_matrix,
	 &SparseMatrix<double>::template precondition_SSOR<double>,
	 1.2);
				   // solve
  cg.solve (mass_matrix, vec, tmp, prec);
  
				   // distribute solution
  constraints.distribute (vec);
};



template <int dim>
void VectorTools::create_right_hand_side (const DoFHandler<dim>    &dof,
					  const Quadrature<dim>    &quadrature,
					  const Function<dim>      &rhs,
					  Vector<double>           &rhs_vector)
{
  Assert (dof.get_fe().n_components() == rhs.n_components,
	  ExcComponentMismatch());
  
  UpdateFlags update_flags = UpdateFlags(update_values   |
					 update_q_points |
					 update_JxW_values);
  SparseMatrix<double> dummy;
  const Assembler<dim>::AssemblerData data (dof,
					    false, true,
					    dummy, rhs_vector,
					    quadrature, update_flags);
  TriaActiveIterator<dim, Assembler<dim> >
    assembler (const_cast<Triangulation<dim>*>(&dof.get_tria()),
	       dof.get_tria().begin_active()->level(),
	       dof.get_tria().begin_active()->index(),
	       &data);
  MassMatrix<dim> equation(&rhs,0);
  do 
    {
      assembler->assemble (equation);
    }
  while ((++assembler).state() == valid);
};




#if deal_II_dimension == 1

template <>
void
VectorTools::interpolate_boundary_values (const DoFHandler<1> &dof,
					  const unsigned char  boundary_component,
					  const Function<1>   &boundary_function,
					  map<int,double>     &boundary_values,
					  const vector<bool>  &component_mask_)
{
  Assert (boundary_component != 255,
	  ExcInvalidBoundaryIndicator());

  const FiniteElement<1> &fe = dof.get_fe();
  Assert (fe.n_components() == boundary_function.n_components,
	  ExcComponentMismatch());
  Assert (fe.dofs_per_vertex == fe.n_components(),
	  ExcComponentMismatch());

				   // set the component mask to either
				   // the original value or a vector
				   // of #true#s
  const vector<bool> component_mask ((component_mask_.size() == 0) ?
				     vector<bool> (fe.n_components(), true) :
				     component_mask_);
  Assert (count(component_mask.begin(), component_mask.end(), true) > 0,
	  ExcComponentMismatch());
  
				   // check whether boundary values at the
				   // left boundary of the line are requested
  if (boundary_component == 0)
    {
				       // first find the leftmost active
				       // cell by first traversing the coarse
				       // grid to its left end and then going
				       // to the children
      DoFHandler<1>::cell_iterator leftmost_cell = dof.begin(0);
      while (leftmost_cell->neighbor(0).state() == valid)
	leftmost_cell = leftmost_cell->neighbor(0);

      while (leftmost_cell->has_children())
	leftmost_cell = leftmost_cell->child(0);

				       // now set the value of the
				       // leftmost degree of
				       // freedom. setting also
				       // created the entry in the map
				       // if it did not exist
				       // beforehand
      for (unsigned int i=0; i<fe.dofs_per_vertex; ++i)
	if (component_mask[fe.face_system_to_component_index(i).first])
	  boundary_values[leftmost_cell->vertex_dof_index(0,i)]
	    = boundary_function.value(leftmost_cell->vertex(0), i);
    };

				   // same for the right boundary of
				   // the line are requested
  if (boundary_component == 1)
    {
				       // first find the leftmost active
				       // cell by first traversing the coarse
				       // grid to its left end and then going
				       // to the children
      DoFHandler<1>::cell_iterator rightmost_cell = dof.last(0);
      while (rightmost_cell->neighbor(1).state() == valid)
	rightmost_cell = rightmost_cell->neighbor(1);

      while (rightmost_cell->has_children())
	rightmost_cell = rightmost_cell->child(1);

				       // now set the value of the rightmost
				       // degree of freedom
      for (unsigned int i=0; i<fe.dofs_per_vertex; ++i)
	if (component_mask[fe.face_system_to_component_index(i).first])
	  boundary_values[rightmost_cell->vertex_dof_index(1,i)]
	    = boundary_function.value(rightmost_cell->vertex(1), i);
    };
  
};


#endif



template <int dim>
void
VectorTools::interpolate_boundary_values (const DoFHandler<dim> &dof,
					  const unsigned char    boundary_component,
					  const Function<dim>   &boundary_function,
					  map<int,double>       &boundary_values,
					  const vector<bool>    &component_mask_)
{
  Assert (boundary_component != 255,
	  ExcInvalidBoundaryIndicator());

  const FiniteElement<dim> &fe           = dof.get_fe();
  const unsigned int        n_components = fe.n_components();
  const bool                fe_is_system = (n_components != 1);
  
  Assert (n_components == boundary_function.n_components,
	  ExcInvalidFE());

				   // set the component mask to either
				   // the original value or a vector
				   // of #true#s
  const vector<bool> component_mask ((component_mask_.size() == 0) ?
				     vector<bool> (fe.n_components(), true) :
				     component_mask_);
  Assert (count(component_mask.begin(), component_mask.end(), true) > 0,
	  ExcComponentMismatch());

				   // field to store the indices of dofs
  vector<int>         face_dofs (fe.dofs_per_face, -1);
  vector<Point<dim> > dof_locations (face_dofs.size(), Point<dim>());
				   // array to store the values of
				   // the boundary function at the
				   // boundary points. have to arrays
				   // for scalar and vector functions
				   // to use the more efficient one
				   // respectively
  vector<double>          dof_values_scalar (fe.dofs_per_face);
  vector<Vector<double> > dof_values_system (fe.dofs_per_face,
					     Vector<double>(fe.n_components()));
	
  DoFHandler<dim>::active_face_iterator face = dof.begin_active_face(),
					endf = dof.end_face();
  for (; face!=endf; ++face)
    if (boundary_component == face->boundary_indicator()) 
				       // face is of the right component
      {
					 // get indices, physical location and
					 // boundary values of dofs on this
					 // face
	face->get_dof_indices (face_dofs);
	fe.get_face_support_points (face, dof_locations);

	if (fe_is_system)
	  {
	    boundary_function.vector_value_list (dof_locations, dof_values_system);
	    
					     // enter into list
	    
	    for (unsigned int i=0; i<face_dofs.size(); ++i)
	      if (component_mask[fe.face_system_to_component_index(i).first])
		boundary_values[face_dofs[i]]
		  = dof_values_system[i](fe.face_system_to_component_index(i).first);
	  }
	else
					   // fe has only one component,
					   // so save some computations
	  {
					     // get only the one component that
					     // this function has
	    boundary_function.value_list (dof_locations,
					      dof_values_scalar,
					      0);
	    
					     // enter into list
	    
	    for (unsigned int i=0; i<face_dofs.size(); ++i)
	      boundary_values[face_dofs[i]] = dof_values_scalar[i];
	  };
      };
}



template <int dim>
void
VectorTools::project_boundary_values (const DoFHandler<dim>    &dof,
				      const map<unsigned char,const Function<dim>*>        &boundary_functions,
				      const Quadrature<dim-1>  &q,
				      map<int,double>          &boundary_values)
{
  Assert (dof.get_fe().n_components() == boundary_functions.begin()->second->n_components,
	  ExcComponentMismatch());
  
  vector<int>    dof_to_boundary_mapping;
  dof.map_dof_to_boundary_indices (boundary_functions, dof_to_boundary_mapping);
  
				   // set up sparsity structure
  SparseMatrixStruct sparsity(dof.n_boundary_dofs(boundary_functions),
			      dof.max_couplings_between_boundary_dofs());
  DoFTools::make_boundary_sparsity_pattern (dof,
					    boundary_functions,
					    dof_to_boundary_mapping,
					    sparsity);

				   // note: for three or more dimensions, there
				   // may be constrained nodes on the boundary
				   // in this case the boundary mass matrix has
				   // to be condensed and the solution is to
				   // be distributed afterwards, which is not
				   // yet implemented. The reason for this is
				   // that we cannot simply use the #condense#
				   // family of functions, since the matrices
				   // and vectors do not use the global
				   // numbering but rather the boundary
				   // numbering, i.e. the condense function
				   // needs to use another indirection. There
				   // should be not many technical problems,
				   // but it needs to be implemented
  if (dim<3)
    sparsity.compress();
  else
    Assert (false, ExcNotImplemented());
  

				   // make mass matrix and right hand side
  SparseMatrix<double> mass_matrix(sparsity);
  Vector<double>       rhs(sparsity.n_rows());
  

  MatrixTools<dim>::create_boundary_mass_matrix (dof, q, 
						 mass_matrix, boundary_functions,
						 rhs, dof_to_boundary_mapping);

				   // same thing as above: if dim>=3 we need
				   // to consider constraints
  Assert (dim<3, ExcNotImplemented());

  
  Vector<double> boundary_projection (rhs.size());

  SolverControl           control(1000, 1e-16);
  PrimitiveVectorMemory<> memory;
  SolverCG<>              cg(control,memory);

  PreconditionRelaxation<>
    prec(mass_matrix,
	 &SparseMatrix<double>::template precondition_SSOR<double>,
	 1.2);
				   // solve
  cg.solve (mass_matrix, boundary_projection, rhs, prec);

				   // fill in boundary values
  for (unsigned int i=0; i<dof_to_boundary_mapping.size(); ++i)
    if (dof_to_boundary_mapping[i] != -1)
				       // this dof is on one of the
				       // interesting boundary parts
				       //
				       // remember: #i# is the global dof
				       // number, #dof_to_boundary_mapping[i]#
				       // is the number on the boundary and
				       // thus in the solution vector
      boundary_values[i] = boundary_projection(dof_to_boundary_mapping[i]);
};



template <int dim>
void
VectorTools::integrate_difference (const DoFHandler<dim>    &dof,
				   const Vector<double>     &fe_function,
				   const Function<dim>      &exact_solution,
				   Vector<float>            &difference,
				   const Quadrature<dim>    &q,
				   const NormType           &norm,
				   const Function<dim>      *weight)
{
  const unsigned int        n_q_points   = q.n_quadrature_points;
  const FiniteElement<dim> &fe           = dof.get_fe();
  const unsigned int        n_components = fe.n_components();
  const bool                fe_is_system = (n_components != 1);

  Assert( !((n_components == 1) && (norm == mean)),
	  ExcNotUseful());

  difference.reinit (dof.get_tria().n_active_cells());
  
  UpdateFlags update_flags = UpdateFlags (update_q_points  |
 					  update_JxW_values);
  if (norm != H1_seminorm)
    update_flags != update_values;
  
  if ((norm==H1_seminorm) || (norm==H1_norm))
    update_flags = UpdateFlags (update_flags | update_gradients);
  
  FEValues<dim> fe_values(fe, q, update_flags);

  vector< Vector<double> >        function_values (n_q_points,
						   Vector<double>(n_components));
  vector<vector<Tensor<1,dim> > > function_grads (n_q_points,
						  vector<Tensor<1,dim> >(n_components));
  vector<double> weight_values (n_q_points);
  
  vector<Vector<double> >         psi_values (n_q_points,
					      Vector<double>(n_components));
  vector<vector<Tensor<1,dim> > > psi_grads (n_q_points,
					     vector<Tensor<1,dim> >(n_components));
  vector<double> psi_scalar (n_q_points);
  vector<double> psi_square (n_q_points);
	    
				   // tmp vector when we use the
				   // Function<dim> functions for
				   // scalar functions
  vector<double>         tmp_values (fe_values.n_quadrature_points);
  vector<Tensor<1,dim> > tmp_gradients (fe_values.n_quadrature_points);
  
 				   // loop over all cells
  DoFHandler<dim>::active_cell_iterator cell = dof.begin_active(),
 					endc = dof.end();
  for (unsigned int index=0; cell != endc; ++cell, ++index)
    {
      double diff=0;
 				       // initialize for this cell
      fe_values.reinit (cell);
      
      switch (norm)
 	{
 	  case mean:
 	  case L1_norm:
 	  case L2_norm:
 	  case Linfty_norm:
 	  case H1_norm:
 	  {
 					     // first compute the exact solution
					     // (vectors) at the quadrature points
					     // try to do this as efficient as
					     // possible by avoiding a second
					     // virtual function call in case
					     // the function really has only
					     // one component
	    if (fe_is_system)
	      exact_solution.vector_value_list (fe_values.get_quadrature_points(),
						psi_values);
	    else
	      {
		exact_solution.value_list (fe_values.get_quadrature_points(),
					   tmp_values);
		for (unsigned int i=0; i<n_q_points; ++i)
		  psi_values[i](0) = tmp_values[i];
	      };
	    
 					     // then subtract finite element
 					     // fe_function
	    fe_values.get_function_values (fe_function, function_values);
	    for (unsigned int q=0; q<n_q_points; ++q)
	      psi_values[q] -= function_values[q];

					     // for L1_norm, Linfty_norm, L2_norm
					     // and H1_norm take square of the
					     // vectors psi[q]. Afterwards
 					     // for L1_norm and Linfty_norm:
 					     // take square root to get finally
					     // the (euclidean) vector norm.
					     // Use psi_scalar to store the squares
					     // of the vectors or the vector norms
					     // respectively.
 	    switch (norm)
	      {
		case mean:
		      break;
		case L1_norm:
 		case Linfty_norm:
 		case L2_norm:
 		case H1_norm:
		      for (unsigned int q=0; q<n_q_points; ++q)
			psi_scalar[q] = psi_values[q].norm_sqr();
		      
		      if (norm == L1_norm || norm == Linfty_norm)
			transform (psi_scalar.begin(), psi_scalar.end(),
				   psi_scalar.begin(), ptr_fun(sqrt));
 		      break;
 		default:
 		      Assert (false, ExcNotImplemented());
 	      };

					     // now weight the values
					     // at the quadrature points due
					     // to the weighting function
	    if (weight)
	      {
		weight->value_list(fe_values.get_quadrature_points(),
				   weight_values);
		for (unsigned int q=0; q<n_q_points; ++q)
		  psi_scalar[q] *= weight_values[q];
	      }

 					     // ok, now we have the integrand,
 					     // let's compute the integral,
 					     // which is
 					     // sum_j psi_j weight_j JxW_j
 					     // (or |psi_j| or |psi_j|^2
 	    switch (norm)
 	      {
 		case mean:
 		case L1_norm:
 		case L2_norm:
		case H1_norm:
		      diff = inner_product (psi_scalar.begin(), psi_scalar.end(),
					    fe_values.get_JxW_values().begin(),
					    0.0);
		      if (norm == L2_norm)
			diff=sqrt(diff);

 		      break;
 		case Linfty_norm:
 		      diff = *max_element (psi_scalar.begin(), psi_scalar.end());
 		      break;
 		default:
 		      Assert (false, ExcNotImplemented());
 	      };

 					     // note: the H1_norm uses the result
 					     // of the L2_norm and control goes
 					     // over to the next case statement!
 	    if (norm != H1_norm)
 	      break;
 	  };

 	  case H1_seminorm:
 	  {
 					     // note: the computation of the
 					     // H1_norm starts at the previous
 					     // case statement, but continues
 					     // here!
					     // Until now, #diff# includes the
					     // square of the L2_norm.

 					     // in praxi: first compute
 					     // exact fe_function vector
					     //
					     // try to be a little clever
					     // to avoid recursive virtual
					     // function calls when calling
					     // #gradient_list# for functions
					     // that are really scalar
					     // functions
	    if (fe_is_system)
	      exact_solution.vector_gradient_list (fe_values.get_quadrature_points(),
						   psi_grads);
	    else
	      {
		exact_solution.gradient_list (fe_values.get_quadrature_points(),
					      tmp_gradients);
		for (unsigned int i=0; i<n_q_points; ++i)
		  psi_grads[i][0] = tmp_gradients[i];
	      };

 					     // then subtract finite element
 					     // function_grads
	    fe_values.get_function_grads (fe_function, function_grads);
	    for (unsigned int k=0; k<n_components; ++k)
	      for (unsigned int q=0; q<n_q_points; ++q)
		psi_grads[q][k] -= function_grads[q][k];

					     // take square of integrand
	    fill_n (psi_square.begin(), n_q_points, 0.0);
	    for (unsigned int k=0; k<n_components; ++k)
	      for (unsigned int q=0; q<n_q_points; ++q)
		psi_square[q] += sqr_point(psi_grads[q][k]);

					     // now weight the values
					     // at the quadrature points due
					     // to the weighting function
	    if (weight)
	      {
		weight->value_list(fe_values.get_quadrature_points(),
				   weight_values);
		for (unsigned int q=0; q<n_q_points; ++q)
		  psi_square[q] *= weight_values[q];
	      }

 					     // add seminorm to L_2 norm or
 					     // to zero
 	    diff += inner_product (psi_square.begin(), psi_square.end(),
				   fe_values.get_JxW_values().begin(),
				   0.0);
 	    diff = sqrt(diff);

 	    break;
 	  };
					     
 	  default:
 		Assert (false, ExcNotImplemented());
 	};

      
 				       // append result of this cell
 				       // to the end of the vector
      difference(index) = diff;
    };
};



void
VectorTools::subtract_mean_value(Vector<double>     &v,
				 const vector<bool> &p_select)
{
  unsigned int n = v.size();
  Assert(n == p_select.size(), ExcDimensionMismatch(n, p_select.size()));

  double       s       = 0;
  unsigned int counter = 0;
  
  for (unsigned int i=0; i<n; ++i)
    if (p_select[i])
      {
	s += v(i);
	++counter;
      };

  s /= counter;
  
  for (unsigned int i=0; i<n; ++i)
    if (p_select[i])
      v(i) -= s;  
};



// explicit instantiations
template
void VectorTools::integrate_difference (const DoFHandler<deal_II_dimension> &,
					const Vector<double>                &,
					const Function<deal_II_dimension>   &,
					Vector<float>                       &,
					const Quadrature<deal_II_dimension> &,
					const NormType                      &,
					const Function<deal_II_dimension>   *);
template
void VectorTools::project_boundary_values (const DoFHandler<deal_II_dimension>  &,
					   const map<unsigned char,const Function<deal_II_dimension>*>&,
					   const Quadrature<deal_II_dimension-1>&,
					   map<int,double>        &);
template
void VectorTools::interpolate_boundary_values (const DoFHandler<deal_II_dimension> &,
					       const unsigned char,
					       const Function<deal_II_dimension>   &,
					       map<int,double>       &,
					       const vector<bool>    &);
template
void VectorTools::create_right_hand_side (const DoFHandler<deal_II_dimension> &,
					const Quadrature<deal_II_dimension>   &,
					const Function<deal_II_dimension>     &,
					Vector<double>                        &);
template
void VectorTools::project (const DoFHandler<deal_II_dimension>   &,
			   const ConstraintMatrix                &,
			   const Quadrature<deal_II_dimension>   &,
			   const Function<deal_II_dimension>     &,
			   Vector<double>                        &,
			   const bool,
			   const Quadrature<deal_II_dimension-1> &,
			   const bool);
template
void VectorTools::interpolate(const DoFHandler<deal_II_dimension> &,
			      const DoFHandler<deal_II_dimension> &,
			      const FullMatrix<double> &,
			      const Vector<double>     &,
			      Vector<double>           &);
template
void VectorTools::interpolate (const DoFHandler<deal_II_dimension> &,
			       const Function<deal_II_dimension>   &,
			       Vector<double>                      &);
