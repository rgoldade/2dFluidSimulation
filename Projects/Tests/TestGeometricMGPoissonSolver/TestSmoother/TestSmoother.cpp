#include <iostream>
#include <memory>
#include <random>
#include <string>

#include "GeometricMultigridOperators.h"
#include "InitialMultigridTestDomains.h"
#include "Renderer.h"
#include "ScalarGrid.h"
#include "Transform.h"
#include "UniformGrid.h"
#include "Utilities.h"

using namespace FluidSim2D::RenderTools;
using namespace FluidSim2D::SimTools;

std::unique_ptr<Renderer> renderer;

static constexpr int gridSize = 256;
static constexpr bool useComplexDomain = true;
static constexpr bool useSolidSphere = true;

static constexpr bool useRandomGuess = true;

static constexpr int maxIterations = 1000;
static constexpr double deltaAmplitude = 1000;

int main(int argc, char** argv)
{
	using namespace GeometricMultigridOperators;

	using StoreReal = double;
	using SolveReal = double;

	UniformGrid<CellLabels> domainCellLabels;
	VectorGrid<StoreReal> boundaryWeights;

	int mgLevels;
	Vec2i exteriorOffset;
	{
		UniformGrid<CellLabels> baseDomainCellLabels;
		VectorGrid<StoreReal> baseBoundaryWeights;

		// Complex domain set up
		if (useComplexDomain)
			buildComplexDomain(baseDomainCellLabels,
								baseBoundaryWeights,
								gridSize,
								useSolidSphere);
		// Simple domain set up
		else
			buildSimpleDomain(baseDomainCellLabels,
								baseBoundaryWeights,
								gridSize,
								1 /*dirichlet band*/);

		// Build expanded domain
		std::pair<Vec2i, int> mgSettings = buildExpandedDomain(domainCellLabels, boundaryWeights, baseDomainCellLabels, baseBoundaryWeights);

		exteriorOffset = mgSettings.first;
		mgLevels = mgSettings.second;
	}

	SolveReal dx = boundaryWeights.dx();

	UniformGrid<StoreReal> rhsGrid(domainCellLabels.size(), 0);
	UniformGrid<StoreReal> solutionGrid(domainCellLabels.size(), 0);
	UniformGrid<StoreReal> residualGrid(domainCellLabels.size(), 0);

	if (useRandomGuess)
	{
		tbb::parallel_for(tbb::blocked_range<int>(0, domainCellLabels.voxelCount(), tbbLightGrainSize), [&](const tbb::blocked_range<int>& range)
		{
			std::default_random_engine generator;
			std::uniform_real_distribution<StoreReal> distribution(0, 1);

			for (int cellIndex = range.begin(); cellIndex != range.end(); ++cellIndex)
			{
				Vec2i cell = domainCellLabels.unflatten(cellIndex);

				if (domainCellLabels(cell) == CellLabels::INTERIOR_CELL ||
					domainCellLabels(cell) == CellLabels::BOUNDARY_CELL)
				{
					solutionGrid(cell) = distribution(generator);
				}
			}
		});
	}

	// Set delta function
	StoreReal deltaPercent = .1;
	Vec2i deltaPoint = Vec2i(deltaPercent * Vec2f(gridSize)) + exteriorOffset;

	forEachVoxelRange(deltaPoint - Vec2i(1), deltaPoint + Vec2i(2), [&](const Vec2i& cell)
	{
		if (domainCellLabels(cell) == CellLabels::INTERIOR_CELL ||
			domainCellLabels(cell) == CellLabels::BOUNDARY_CELL)
			rhsGrid(cell) = deltaAmplitude;
	});
	
	float oldLInfinityError = std::numeric_limits<float>::max();
	float oldL2Error = oldLInfinityError;

	std::vector<Vec2i> boundaryCells = buildBoundaryCells(domainCellLabels, 3);

	std::cout.precision(10);

	for (int iteration = 0; iteration < maxIterations; ++iteration)
	{
		boundaryJacobiPoissonSmoother<SolveReal>(solutionGrid, rhsGrid, domainCellLabels, boundaryCells, dx, &boundaryWeights);

		interiorJacobiPoissonSmoother<SolveReal>(solutionGrid, rhsGrid, domainCellLabels, dx, &boundaryWeights);

		boundaryJacobiPoissonSmoother<SolveReal>(solutionGrid, rhsGrid, domainCellLabels, boundaryCells, dx, &boundaryWeights);

		computePoissonResidual<SolveReal>(residualGrid, solutionGrid, rhsGrid, domainCellLabels, dx);

		SolveReal lInfinityError = lInfinityNorm<SolveReal>(residualGrid, domainCellLabels);
		SolveReal l2Error = squaredl2Norm<SolveReal>(residualGrid, domainCellLabels);

		if (oldLInfinityError < lInfinityError)
			std::cout << "L-Infinity error didn't decrease" << std::endl;
		if (oldL2Error < l2Error)
			std::cout << "L-2 error didn't decrease" << std::endl;

		oldLInfinityError = lInfinityError;
		oldL2Error = l2Error;
		std::cout << "Iteration: " << iteration << ". L-infinity error: " << lInfinityError << ". L-2 error: " << std::sqrt(l2Error) << std::endl;
	}

	// Print domain labels to make sure they are set up correctly
	int pixelHeight = 1080;
	int pixelWidth = pixelHeight;
	renderer = std::make_unique<Renderer>("Smoother Test", Vec2i(pixelWidth, pixelHeight), Vec2f(0), 1, &argc, argv);

	ScalarGrid<float> tempGrid(Transform(dx, Vec2f(0)), domainCellLabels.size());

	tbb::parallel_for(tbb::blocked_range<int>(0, tempGrid.voxelCount(), tbbLightGrainSize), [&](const tbb::blocked_range<int>& range)
	{
		for (int cellIndex = range.begin(); cellIndex != range.end(); ++cellIndex)
		{
			Vec2i cell = tempGrid.unflatten(cellIndex);

			tempGrid(cell) = float(domainCellLabels(cell));
		}
	});

	tempGrid.drawVolumetric(*renderer, Vec3f(0), Vec3f(1), float(CellLabels::INTERIOR_CELL), float(CellLabels::BOUNDARY_CELL));

	renderer->run();
}