#pragma once

#include "Core.h"
#include "Vec.h"

#include "ScalarGrid.h"
#include "VectorGrid.h"
#include "LevelSet2D.h"
#include "Transform.h"

#include "Integrator.h"
#include "Solver.h"

class AnalyticalPoissonSolver
{
public:
	AnalyticalPoissonSolver(const Transform& xform, const Vec2st& nx)
		: m_xform(xform)
	{
		m_poissongrid = ScalarGrid<Real>(m_xform, nx, 0);
	}

	template<typename initial_functor, typename solution_functor>
	Real solve(initial_functor initial, solution_functor solution);

	void draw_grid(Renderer& renderer) const;
	void draw_values(Renderer& renderer) const;

private:
	
	Transform m_xform;

	ScalarGrid<Real> m_poissongrid;
};

template<typename initial_functor, typename solution_functor>
Real AnalyticalPoissonSolver::solve(initial_functor initial, solution_functor solution)
{
	UniformGrid<int> solvable_cells(m_poissongrid.size(), 0);

	int solvecount = 0;

	Vec2st size = solvable_cells.size();
	for (int i = 0; i < size[0]; ++i)
		for (int j = 0; j < size[1]; ++j)
		{
			solvable_cells(i, j) = solvecount++;
		}

	Solver solver(solvecount, solvecount * 5);

	Real dx = m_poissongrid.dx();
	Real coeff = 1. / sqr(dx);

	for (int i = 0; i < size[0]; ++i)
		for (int j = 0; j < size[1]; ++j)
		{
			int idx = solvable_cells(i, j);
			if (idx >= 0)
			{
				Vec2i cell(i, j);

				// Build RHS
				Vec2R pos = m_poissongrid.idx_to_ws(Vec2R(i,j));
				solver.add_rhs(idx, initial(pos));

				// Build row
				Real middle_coeff = 0;

				for (int dir = 0; dir < 4; ++dir)
				{
					Vec2i ncell = cell + cell_offset[dir];
					
					// Bounds check. Use analytical solution for Dirichlet condition.
					if (ncell[0] < 0 || ncell[1] < 0 || 
						ncell[0] >= size[0] || ncell[1] >= size[1]) 
					{
						Vec2R npos = m_poissongrid.idx_to_ws(Vec2R(ncell));
						solver.add_rhs(idx, solution(npos));
					}
					else
					{
						// If neighbouring cell is solvable, it should have an entry in the system
						int cidx = solvable_cells(ncell[0], ncell[1]);

						if (cidx >= 0)
						{
							solver.add_element(idx, cidx, -coeff);
						}	
					}
					middle_coeff += coeff;
				}

				if (middle_coeff > 0.)	solver.add_element(idx, idx, middle_coeff);
				else solver.add_element(idx, idx, 1.);
			}
		}

	bool solved = solver.solve();

	if (!solved)
	{
		std::cout << "Analytical poisson test failed to solve" << std::endl;
		return -1;
	}

	Real error = 0;

	for (int i = 0; i < size[0]; ++i)
		for (int j = 0; j < size[1]; ++j)
		{
			int idx = solvable_cells(i, j);

			if (idx >= 0)
			{
				Vec2R pos = m_poissongrid.idx_to_ws(Vec2R(i, j));
				Real temperror = fabs(solver.sol(idx) - solution(pos));

				if (error < temperror) error = temperror;

				m_poissongrid(i, j) = solver.sol(idx);
			}
		}

}