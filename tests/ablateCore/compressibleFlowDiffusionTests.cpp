static char help[] = "1D conduction and diffusion cases compared to exact solution";

#include <compressibleFlow.h>
#include <petsc.h>
#include <cmath>
#include <vector>
#include "MpiTestFixture.hpp"
#include "gtest/gtest.h"

typedef struct {
    PetscInt dim;
    PetscReal L;
    PetscReal gamma;
    PetscReal Rgas;
    PetscReal k;
    PetscReal rho;
    PetscReal Tinit;
    PetscReal Tboundary;
} InputParameters;

struct CompressibleFlowDiffusionTestParameters {
    testingResources::MpiTestParameter mpiTestParameter;
    InputParameters parameters;
    PetscInt initialNx;
    int levels;
    std::vector<PetscReal> expectedL2Convergence;
    std::vector<PetscReal> expectedLInfConvergence;
};

class CompressibleFlowDiffusionTestFixture : public testingResources::MpiTestFixture, public ::testing::WithParamInterface<CompressibleFlowDiffusionTestParameters> {
   public:
    void SetUp() override { SetMpiParameters(GetParam().mpiTestParameter); }
};

/**
 * Simple function to compute the exact solution for a given xyz and time
 */
static PetscReal ComputeTExact(PetscReal time, const PetscReal xyz[], InputParameters *parameters) {
    // compute cv for a perfect gas
    PetscReal cv = parameters->gamma * parameters->Rgas / (parameters->gamma - 1) - parameters->Rgas;

    // compute the alpha in the equation
    PetscReal alpha = parameters->k / (parameters->rho * cv);
    PetscReal Tinitial = (parameters->Tinit - parameters->Tboundary);
    PetscReal T = 0.0;
    for (PetscReal n = 1; n < 2000; n++) {
        PetscReal Bn = -Tinitial * 2.0 * (-1.0 + PetscPowReal(-1.0, n)) / (n * PETSC_PI);
        T += Bn * PetscSinReal(n * PETSC_PI * xyz[0] / parameters->L) * PetscExpReal(-n * n * PETSC_PI * PETSC_PI * alpha * time / (PetscSqr(parameters->L)));
    }

    return T + parameters->Tboundary;
}

static PetscErrorCode EulerExact(PetscInt dim, PetscReal time, const PetscReal xyz[], PetscInt Nf, PetscScalar *node, void *ctx) {
    PetscFunctionBeginUser;

    InputParameters *parameters = (InputParameters *)ctx;

    PetscReal T = ComputeTExact(time, xyz, parameters);

    PetscReal u = 0.0;
    PetscReal v = 0.0;
    PetscReal p = parameters->rho * parameters->Rgas * T;
    PetscReal e = p / ((parameters->gamma - 1.0) * parameters->rho);
    PetscReal eT = e + 0.5 * (u * u + v * v);

    node[RHO] = parameters->rho;
    node[RHOE] = parameters->rho * eT;
    node[RHOU + 0] = parameters->rho * u;
    node[RHOU + 1] = parameters->rho * v;

    PetscFunctionReturn(0);
}

static PetscErrorCode PhysicsBoundary_Euler(PetscReal time, const PetscReal *c, const PetscReal *n, const PetscScalar *a_xI, PetscScalar *a_xG, void *ctx) {
    PetscFunctionBeginUser;
    InputParameters *parameters = (InputParameters *)ctx;

    // compute the centroid location of the real cell
    // Offset the calc assuming the cells are square
    PetscReal x[3];
    for (PetscInt i = 0; i < parameters->dim; i++) {
        x[i] = c[i] - n[i] * 0.5;
    }

    // compute the temperature of the real inside cell
    PetscReal Tinside = ComputeTExact(time, x, parameters);

    PetscReal T = parameters->Tboundary - (Tinside - parameters->Tboundary);
    PetscReal u = 0.0;
    PetscReal v = 0.0;
    PetscReal p = parameters->rho * parameters->Rgas * T;
    PetscReal e = p / ((parameters->gamma - 1.0) * parameters->rho);
    PetscReal eT = e + 0.5 * (u * u + v * v);

    a_xG[RHO] = parameters->rho;
    a_xG[RHOE] = parameters->rho * eT;
    a_xG[RHOU + 0] = parameters->rho * u;
    a_xG[RHOU + 1] = parameters->rho * v;

    PetscFunctionReturn(0);
}

static PetscErrorCode PhysicsBoundary_Mirror(PetscReal time, const PetscReal *c, const PetscReal *n, const PetscScalar *a_xI, PetscScalar *a_xG, void *ctx) {
    PetscFunctionBeginUser;
    InputParameters *constants = (InputParameters *)ctx;

    // Offset the calc assuming the cells are square
    for (PetscInt f = 0; f < RHOU + constants->dim; f++) {
        a_xG[f] = a_xI[f];
    }
    PetscFunctionReturn(0);
}

static void ComputeErrorNorms(TS ts, FlowData flowData, std::vector<PetscReal> &residualNorm2, std::vector<PetscReal> &residualNormInf, InputParameters *parameters,
                              testingResources::PetscTestErrorChecker &errorChecker) {
    // Compute the error
    PetscDS ds;
    DMGetDS(flowData->dm, &ds) >> errorChecker;

    // Get the current time
    PetscReal time;
    TSGetTime(ts, &time) >> errorChecker;

    // Get the exact solution
    void *exactCtxs[1];
    PetscErrorCode (*exactFuncs[1])(PetscInt dim, PetscReal time, const PetscReal x[], PetscInt Nf, PetscScalar *u, void *ctx);
    PetscDSGetExactSolution(ds, 0, &exactFuncs[0], &exactCtxs[0]) >> errorChecker;

    // get the fvm and the number of fields
    PetscFV fvm;
    DMGetField(flowData->dm, 0, NULL, (PetscObject *)&fvm) >> errorChecker;
    PetscInt components;
    PetscFVGetNumComponents(fvm, &components) >> errorChecker;

    // Size the error values
    residualNorm2.resize(components);
    residualNormInf.resize(components);

    // Create an vector to hold the exact solution
    Vec exactVec;
    VecDuplicate(flowData->flowField, &exactVec) >> errorChecker;
    DMProjectFunction(flowData->dm, time, exactFuncs, exactCtxs, INSERT_ALL_VALUES, exactVec) >> errorChecker;
    PetscObjectSetName((PetscObject)exactVec, "exact") >> errorChecker;

    // Compute the error
    VecAXPY(exactVec, -1.0, flowData->flowField) >> errorChecker;
    VecSetBlockSize(exactVec, components);
    PetscInt size;
    VecGetSize(exactVec, &size) >> errorChecker;

    // Compute the l2 errors
    VecStrideNormAll(exactVec, NORM_2, &residualNorm2[0]) >> errorChecker;
    // normalize by the number of nodes
    for (auto i = 0; i < residualNorm2.size(); i++) {
        residualNorm2[i] *= PetscSqrtReal(1.0 / (size / components));
    }

    // And the Inf form
    VecStrideNormAll(exactVec, NORM_INFINITY, &residualNormInf[0]) >> errorChecker;
    VecDestroy(&exactVec) >> errorChecker;
}
//
// static PetscErrorCode MonitorError(TS ts, PetscInt step, PetscReal time, Vec u, void *ctx) {
//    PetscFunctionBeginUser;
//    PetscErrorCode     ierr;
//
//    FlowData flowData = (FlowData)ctx;
//
//    // Get the DM
//    DM dm;
//    ierr = TSGetDM(ts, &dm);
//    CHKERRQ(ierr);
//
//    // Open a vtk viewer
//    //    PetscViewer viewer;
//    //    char        filename[PETSC_MAX_PATH_LEN];
//    //    ierr = PetscSNPrintf(filename,sizeof(filename),"/Users/mcgurn/chrestScratch/results/vortex/flow%.4D.vtu",step);CHKERRQ(ierr);
//    //    ierr = PetscViewerVTKOpen(PetscObjectComm((PetscObject)dm),filename,FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
//    //    ierr = VecView(u,viewer);CHKERRQ(ierr);
//    //    ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);
//
//    ierr = PetscPrintf(PetscObjectComm((PetscObject)dm), "TS at %f\n", time);
//    CHKERRQ(ierr);
//
//    // Compute the error
//    void *exactCtxs[1];
//    PetscErrorCode (*exactFuncs[1])(PetscInt dim, PetscReal time, const PetscReal x[], PetscInt Nf, PetscScalar *u, void *ctx);
//    PetscDS ds;
//    ierr = DMGetDS(dm, &ds);
//    CHKERRQ(ierr);
//
//    // Get the exact solution
//    ierr = PetscDSGetExactSolution(ds, 0, &exactFuncs[0], &exactCtxs[0]);
//    CHKERRQ(ierr);
//
//
//    VecView(u, PETSC_VIEWER_STDOUT_WORLD);
//
//    // Create an vector to hold the exact solution
//    Vec exactVec;
//    ierr = VecDuplicate(u, &exactVec);
//    CHKERRQ(ierr);
//    ierr = DMProjectFunction(dm, time, exactFuncs, exactCtxs, INSERT_ALL_VALUES, exactVec);
//    CHKERRQ(ierr);
//
//    ierr = PetscObjectSetName((PetscObject)exactVec, "exact");
//    CHKERRQ(ierr);
//    ierr = VecViewFromOptions(exactVec, NULL, "-sol_view");
//    CHKERRQ(ierr);
//
//    // For each component, compute the l2 norms
//    ierr = VecAXPY(exactVec, -1.0, u);
//    CHKERRQ(ierr);
//
//    PetscReal ferrors[4];
//    ierr = VecSetBlockSize(exactVec, 4);
//    CHKERRQ(ierr);
//
//    ierr = PetscPrintf(PETSC_COMM_WORLD, "Timestep: %04d time = %-8.4g \t\n", (int)step, (double)time);
//    CHKERRQ(ierr);
//    ierr = VecStrideNormAll(exactVec, NORM_2, ferrors);
//    CHKERRQ(ierr);
//    ierr = PetscPrintf(PETSC_COMM_WORLD, "\tL_2 Error: [%2.3g, %2.3g, %2.3g, %2.3g]\n", (double)(ferrors[0]), (double)(ferrors[1]), (double)(ferrors[2]), (double)(ferrors[3]));
//    CHKERRQ(ierr);
//
//    // And the infinity error
//    ierr = VecStrideNormAll(exactVec, NORM_INFINITY, ferrors);
//    CHKERRQ(ierr);
//    ierr = PetscPrintf(PETSC_COMM_WORLD, "\tL_Inf Error: [%2.3g, %2.3g, %2.3g, %2.3g]\n", (double)ferrors[0], (double)ferrors[1], (double)ferrors[2], (double)ferrors[3]);
//    CHKERRQ(ierr);
//
//    ierr = PetscObjectSetName((PetscObject)exactVec, "error");
//    CHKERRQ(ierr);
//    ierr = VecViewFromOptions(exactVec, NULL, "-sol_view");
//    CHKERRQ(ierr);
//
//    ierr = VecDestroy(&exactVec);
//    CHKERRQ(ierr);
//    PetscFunctionReturn(0);
//}
//

TEST_P(CompressibleFlowDiffusionTestFixture, ShouldConvergeToExactSolution) {
    StartWithMPI
        PetscErrorCode ierr;

        // initialize petsc and mpi
        PetscInitialize(argc, argv, NULL, "HELP") >> errorChecker;

        InputParameters parameters = GetParam().parameters;
        parameters.dim = 2;
        PetscInt blockSize = 2 + parameters.dim;
        PetscInt initialNx = GetParam().initialNx;

        std::vector<PetscReal> hHistory;
        std::vector<std::vector<PetscReal>> l2History(blockSize);
        std::vector<std::vector<PetscReal>> lInfHistory(blockSize);

        // March over each level
        for (PetscInt l = 0; l < GetParam().levels; l++) {
            PetscPrintf(PETSC_COMM_WORLD, "Running RHS Calculation at Level %d\n", l);

            DM dm; /* problem definition */
            TS ts; /* timestepper */

            // Create a ts
            TSCreate(PETSC_COMM_WORLD, &ts) >> errorChecker;
            TSSetProblemType(ts, TS_NONLINEAR) >> errorChecker;
            TSSetType(ts, TSEULER) >> errorChecker;
            TSSetExactFinalTime(ts, TS_EXACTFINALTIME_MATCHSTEP) >> errorChecker;
            TSSetFromOptions(ts) >> errorChecker;

            // Create a mesh
            // hard code the problem setup
            PetscReal start[] = {0.0, 0.0};
            PetscReal end[] = {parameters.L, parameters.L};
            PetscInt nx1D = initialNx * PetscPowRealInt(2, l);
            PetscInt nx[] = {nx1D, nx1D};
            DMBoundaryType bcType[] = {DM_BOUNDARY_NONE, DM_BOUNDARY_NONE};
            DMPlexCreateBoxMesh(PETSC_COMM_WORLD, parameters.dim, PETSC_FALSE, nx, start, end, bcType, PETSC_TRUE, &dm) >> errorChecker;

            // Setup the flow data
            FlowData flowData; /* store some of the flow data*/
            FlowCreate(&flowData) >> errorChecker;

            // Setup
            CompressibleFlow_SetupDiscretization(flowData, &dm);

            // Add in the flow parameters
            PetscScalar params[TOTAL_COMPRESSIBLE_FLOW_PARAMETERS];
            params[CFL] = 0.5;
            params[GAMMA] = parameters.gamma;
            params[RGAS] = parameters.Rgas;
            params[K] = parameters.k;

            // set up the finite volume fluxes
            CompressibleFlow_StartProblemSetup(flowData, TOTAL_COMPRESSIBLE_FLOW_PARAMETERS, params) >> errorChecker;

            // Add in any boundary conditions
            PetscDS prob;
            ierr = DMGetDS(flowData->dm, &prob);
            CHKERRABORT(PETSC_COMM_WORLD, ierr);
            const PetscInt idsLeft[] = {2, 4};
            PetscDSAddBoundary(prob, DM_BC_NATURAL_RIEMANN, "wall left", "Face Sets", 0, 0, NULL, (void (*)(void))PhysicsBoundary_Euler, NULL, 2, idsLeft, &parameters) >> errorChecker;

            const PetscInt idsTop[] = {1, 3};
            PetscDSAddBoundary(prob, DM_BC_NATURAL_RIEMANN, "top/bottom", "Face Sets", 0, 0, NULL, (void (*)(void))PhysicsBoundary_Mirror, NULL, 2, idsTop, &parameters) >> errorChecker;

            // Complete the problem setup
            CompressibleFlow_CompleteProblemSetup(flowData, ts) >> errorChecker;

            // Name the flow field
            PetscObjectSetName(((PetscObject)flowData->flowField), "Numerical Solution") >> errorChecker;

            // Setup the TS
            TSSetFromOptions(ts) >> errorChecker;

            // set the initial conditions
            PetscErrorCode (*func[2])(PetscInt dim, PetscReal time, const PetscReal x[], PetscInt Nf, PetscScalar *u, void *ctx) = {EulerExact};
            void *ctxs[1] = {&parameters};
            DMProjectFunction(flowData->dm, 0.0, func, ctxs, INSERT_ALL_VALUES, flowData->flowField) >> errorChecker;

            // for the mms, add the exact solution
            PetscDSSetExactSolution(prob, 0, EulerExact, &parameters) >> errorChecker;

            // advance to the end time
            TSSolve(ts, flowData->flowField) >> errorChecker;

            // Get the L2 and LInf norms
            std::vector<PetscReal> l2Norm;
            std::vector<PetscReal> lInfNorm;

            // Compute the error
            ComputeErrorNorms(ts, flowData, l2Norm, lInfNorm, &parameters, errorChecker);

            // print the results to help with debug
            auto l2String = PrintVector(l2Norm, "%2.3g");
            PetscPrintf(PETSC_COMM_WORLD, "\tL_2 Error: %s\n", l2String.c_str()) >> errorChecker;

            auto lInfString = PrintVector(lInfNorm, "%2.3g");
            PetscPrintf(PETSC_COMM_WORLD, "\tL_2 L_Inf: %s\n", lInfString.c_str()) >> errorChecker;

            // Store the residual into history
            hHistory.push_back(PetscLog10Real(parameters.L / nx1D));
            for (auto b = 0; b < blockSize; b++) {
                l2History[b].push_back(PetscLog10Real(l2Norm[b]));
                lInfHistory[b].push_back(PetscLog10Real(lInfNorm[b]));
            }

            FlowDestroy(&flowData) >> errorChecker;
            TSDestroy(&ts) >> errorChecker;
        }

        // Fit each component and output
        for (auto b = 0; b < blockSize; b++) {
            PetscReal l2Slope;
            PetscReal l2Intercept;
            PetscLinearRegression(hHistory.size(), &hHistory[0], &l2History[b][0], &l2Slope, &l2Intercept) >> errorChecker;

            PetscReal lInfSlope;
            PetscReal lInfIntercept;
            PetscLinearRegression(hHistory.size(), &hHistory[0], &lInfHistory[b][0], &lInfSlope, &lInfIntercept) >> errorChecker;

            PetscPrintf(PETSC_COMM_WORLD, "RHS Convergence[%d]: L2 %2.3g LInf %2.3g \n", b, l2Slope, lInfSlope) >> errorChecker;

            if (std::isnan(GetParam().expectedL2Convergence[b])) {
                ASSERT_TRUE(std::isnan(l2Slope)) << "incorrect L2 convergence order for component[" << b << "]";
            } else {
                ASSERT_NEAR(l2Slope, GetParam().expectedL2Convergence[b], 0.2) << "incorrect L2 convergence order for component[" << b << "]";
            }
            if (std::isnan(GetParam().expectedLInfConvergence[b])) {
                ASSERT_TRUE(std::isnan(lInfSlope)) << "incorrect LInf convergence order for component[" << b << "]";
            } else {
                ASSERT_NEAR(lInfSlope, GetParam().expectedLInfConvergence[b], 0.2) << "incorrect LInf convergence order for component[" << b << "]";
            }
        }

        ierr = PetscFinalize();
        exit(ierr);

    EndWithMPI
}

INSTANTIATE_TEST_SUITE_P(
    CompressibleFlow, CompressibleFlowDiffusionTestFixture,
    testing::Values((CompressibleFlowDiffusionTestParameters){
        .mpiTestParameter = {.testName = "conduction",
                             .nproc = 1,
                             .arguments = "-dm_plex_separate_marker -petsclimiter_type none -ts_adapt_type none -flux_diff off -automaticTimeStepCalculator off -ts_max_steps 600 -ts_dt 0.00000625 "},
        .parameters = {.dim = 2, .L = 0.2, .gamma = 1.4, .Rgas = 1.0, .k = 0.3, .rho = 1.0, .Tinit = 400, .Tboundary = 300},
        .initialNx = 4,
        .levels = 3,
        .expectedL2Convergence = {NAN, 1.5, NAN, NAN},
        .expectedLInfConvergence = {NAN, 1.3, NAN, NAN}}),
    [](const testing::TestParamInfo<CompressibleFlowDiffusionTestParameters> &info) { return info.param.mpiTestParameter.getTestName(); });