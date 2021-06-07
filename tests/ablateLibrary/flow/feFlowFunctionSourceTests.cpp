static char help[] =
    "Time-dependent Low Mach Flow in 2d channels with finite elements.\n\
We solve the Low Mach flow problem in a rectangular\n\
domain, using a parallel unstructured mesh (DMPLEX) to discretize it.\n\n\n";

#include <petsc.h>
#include <flow/incompressibleFlow.hpp>
#include <memory>
#include "MpiTestFixture.hpp"
#include "flow/lowMachFlow.hpp"
#include "gtest/gtest.h"
#include "mesh.h"
#include "mesh/dmWrapper.hpp"
#include "parameters/petscOptionParameters.hpp"

// We can define them because they are the same between fe flows
#define VTEST 0
#define QTEST 1
#define WTEST 2

#define VEL 0
#define PRES 1
#define TEMP 2

typedef PetscErrorCode (*ExactFunction)(PetscInt dim, PetscReal time, const PetscReal x[], PetscInt Nf, PetscScalar *u, void *ctx);

typedef void (*IntegrandTestFunction)(PetscInt dim, PetscInt Nf, PetscInt NfAux, const PetscInt *uOff, const PetscInt *uOff_x, const PetscScalar *u, const PetscScalar *u_t, const PetscScalar *u_x,
                                      const PetscInt *aOff, const PetscInt *aOff_x, const PetscScalar *a, const PetscScalar *a_t, const PetscScalar *a_x, PetscReal t, const PetscReal *X,
                                      PetscInt numConstants, const PetscScalar *constants, PetscScalar *f0);

#define SourceFunction(FUNC)            \
    FUNC(PetscInt dim,                  \
         PetscInt Nf,                   \
         PetscInt NfAux,                \
         const PetscInt uOff[],         \
         const PetscInt uOff_x[],       \
         const PetscScalar u[],         \
         const PetscScalar u_t[],       \
         const PetscScalar u_x[],       \
         const PetscInt aOff[],         \
         const PetscInt aOff_x[],       \
         const PetscScalar a[],         \
         const PetscScalar a_t[],       \
         const PetscScalar a_x[],       \
         PetscReal t,                   \
         const PetscReal X[],           \
         PetscInt numConstants,         \
         const PetscScalar constants[], \
         PetscScalar f0[])

// store the pointer to the provided test function from the solver
static IntegrandTestFunction f0_v_original;
static IntegrandTestFunction f0_w_original;
static IntegrandTestFunction f0_q_original;

struct FEFlowMMSParameters {
    testingResources::MpiTestParameter mpiTestParameter;
    std::function<std::shared_ptr<ablate::flow::Flow>(std::shared_ptr<ablate::mesh::Mesh>, std::shared_ptr<ablate::parameters::Parameters>)> createMethod;
    ExactFunction uExact;
    ExactFunction pExact;
    ExactFunction TExact;
    ExactFunction u_tExact;
    ExactFunction T_tExact;
    IntegrandTestFunction f0_v;
    IntegrandTestFunction f0_w;
    IntegrandTestFunction f0_q;
};

class FEFlowMMSTestFixture : public testingResources::MpiTestFixture, public ::testing::WithParamInterface<FEFlowMMSParameters> {
   public:
    void SetUp() override { SetMpiParameters(GetParam().mpiTestParameter); }
};

static PetscErrorCode SetInitialConditions(TS ts, Vec u) {
    DM dm;
    PetscReal t;
    PetscErrorCode ierr;

    PetscFunctionBegin;
    ierr = TSGetDM(ts, &dm);
    CHKERRQ(ierr);
    ierr = TSGetTime(ts, &t);
    CHKERRQ(ierr);

    // Get the flowData
    ablate::flow::Flow *flow;
    ierr = DMGetApplicationContext(dm, &flow);
    CHKERRQ(ierr);

    // This function Tags the u vector as the exact solution.  We need to copy the values to prevent this.
    Vec e;
    ierr = VecDuplicate(u, &e);
    CHKERRQ(ierr);
    ierr = DMComputeExactSolution(dm, t, e, NULL);
    CHKERRQ(ierr);
    ierr = VecCopy(e, u);
    CHKERRQ(ierr);
    ierr = VecDestroy(&e);
    CHKERRQ(ierr);

    // get the flow to apply the completeFlowInitialization method
    flow->CompleteFlowInitialization(dm, u);
    CHKERRQ(ierr);

    PetscFunctionReturn(0);
}

static PetscErrorCode MonitorError(TS ts, PetscInt step, PetscReal crtime, Vec u, void *ctx) {
    PetscErrorCode (*exactFuncs[3])(PetscInt dim, PetscReal time, const PetscReal x[], PetscInt Nf, PetscScalar *u, void *ctx);
    void *ctxs[3];
    DM dm;
    PetscDS ds;
    Vec v;
    PetscReal ferrors[3];
    PetscInt f;
    PetscErrorCode ierr;

    PetscFunctionBeginUser;
    ierr = TSGetDM(ts, &dm);
    CHKERRQ(ierr);
    ierr = DMGetDS(dm, &ds);
    CHKERRQ(ierr);

    for (f = 0; f < 3; ++f) {
        ierr = PetscDSGetExactSolution(ds, f, &exactFuncs[f], &ctxs[f]);
        CHKERRABORT(PETSC_COMM_WORLD, ierr);
    }
    ierr = DMComputeL2FieldDiff(dm, crtime, exactFuncs, ctxs, u, ferrors);
    CHKERRABORT(PETSC_COMM_WORLD, ierr);
    ierr = PetscPrintf(PETSC_COMM_WORLD, "Timestep: %04d time = %-8.4g \t L_2 Error: [%2.3g, %2.3g, %2.3g]\n", (int)step, (double)crtime, (double)ferrors[0], (double)ferrors[1], (double)ferrors[2]);
    CHKERRABORT(PETSC_COMM_WORLD, ierr);

    ierr = DMGetGlobalVector(dm, &u);
    CHKERRABORT(PETSC_COMM_WORLD, ierr);
    // ierr = TSGetSolution(ts, &u);CHKERRABORT(PETSC_COMM_WORLD, ierr);
    ierr = PetscObjectSetName((PetscObject)u, "Numerical Solution");
    CHKERRABORT(PETSC_COMM_WORLD, ierr);
    ierr = VecViewFromOptions(u, NULL, "-sol_vec_view");
    CHKERRABORT(PETSC_COMM_WORLD, ierr);
    ierr = DMRestoreGlobalVector(dm, &u);
    CHKERRABORT(PETSC_COMM_WORLD, ierr);

    ierr = DMGetGlobalVector(dm, &v);
    CHKERRABORT(PETSC_COMM_WORLD, ierr);
    // ierr = VecSet(v, 0.0);CHKERRABORT(PETSC_COMM_WORLD, ierr);
    ierr = DMProjectFunction(dm, 0.0, exactFuncs, ctxs, INSERT_ALL_VALUES, v);
    CHKERRABORT(PETSC_COMM_WORLD, ierr);
    ierr = PetscObjectSetName((PetscObject)v, "Exact Solution");
    CHKERRABORT(PETSC_COMM_WORLD, ierr);
    ierr = VecViewFromOptions(v, NULL, "-exact_vec_view");
    CHKERRABORT(PETSC_COMM_WORLD, ierr);
    ierr = DMRestoreGlobalVector(dm, &v);
    CHKERRABORT(PETSC_COMM_WORLD, ierr);

    PetscFunctionReturn(0);
}

// helper functions for generated code
static PetscReal Power(PetscReal x, PetscInt exp) { return PetscPowReal(x, exp); }
static PetscReal Cos(PetscReal x) { return PetscCosReal(x); }
static PetscReal Sin(PetscReal x) { return PetscSinReal(x); }

/*
  CASE: low mach quadratic
  In 2D we use exact solution:

    u = t + x^2 + y^2
    v = t + 2x^2 + 2xy
    p = x + y - 1
    T = t + x + y +1
    z = {0, 1}

  see docs/content/formulations/lowMachFlow/solutions/LowMach_2D_Quadratic_MMS.nb.nb

*/
static PetscErrorCode lowMach_quadratic_u(PetscInt Dim, PetscReal time, const PetscReal X[], PetscInt Nf, PetscScalar *u, void *ctx) {
    // u = {t + x^2 + y^2, t + 2*x^2 + 2*x*y}
    u[0] = time + X[0] * X[0] + X[1] * X[1];
    u[1] = time + 2.0 * X[0] * X[0] + 2.0 * X[0] * X[1];
    return 0;
}
static PetscErrorCode lowMach_quadratic_u_t(PetscInt Dim, PetscReal time, const PetscReal X[], PetscInt Nf, PetscScalar *u, void *ctx) {
    u[0] = 1.0;
    u[1] = 1.0;
    return 0;
}

static PetscErrorCode lowMach_quadratic_p(PetscInt Dim, PetscReal time, const PetscReal X[], PetscInt Nf, PetscScalar *p, void *ctx) {
    // p = x + y - 1
    p[0] = X[0] + X[1] - 1.0;
    return 0;
}

static PetscErrorCode lowMach_quadratic_T(PetscInt Dim, PetscReal time, const PetscReal X[], PetscInt Nf, PetscScalar *T, void *ctx) {
    // T = t + x + y + 1
    T[0] = time + X[0] + X[1] + 1;
    return 0;
}
static PetscErrorCode lowMach_quadratic_T_t(PetscInt Dim, PetscReal time, const PetscReal X[], PetscInt Nf, PetscScalar *T, void *ctx) {
    T[0] = 1.0;
    return 0;
}

static void SourceFunction(f0_lowMach_quadratic_q) {
    f0_q_original(dim, Nf, NfAux, uOff, uOff_x, u, u_t, u_x, aOff, aOff_x, a, a_t, a_x, t, X, numConstants, constants, f0);
    const PetscReal S = constants[0];    // STROUHAL
    const PetscReal Pth = constants[6];  // PTH
    const PetscReal x = X[0];
    const PetscReal y = X[1];

    f0[0] -= -((Pth * S) / Power(1 + t + x + y, 2)) + (4 * Pth * x) / (1 + t + x + y) - (Pth * (t + 2 * Power(x, 2) + 2 * x * y)) / Power(1 + t + x + y, 2) -
             (Pth * (t + Power(x, 2) + Power(y, 2))) / Power(1 + t + x + y, 2);
}

static void SourceFunction(f0_lowMach_quadratic_v) {
    f0_v_original(dim, Nf, NfAux, uOff, uOff_x, u, u_t, u_x, aOff, aOff_x, a, a_t, a_x, t, X, numConstants, constants, f0);

    const PetscReal S = constants[0];    // STROUHAL
    const PetscReal Pth = constants[6];  // PTH
    const PetscReal mu = constants[7];   // MU
    const PetscReal R = constants[1];    // REYNOLDS
    const PetscReal F = constants[2];    // FROUDE

    const PetscReal x = X[0];
    const PetscReal y = X[1];

    f0[0] -= 1 - (5.333333333333334 * mu) / R + (Pth * S) / (1 + t + x + y) + (2 * Pth * y * (t + 2 * Power(x, 2) + 2 * x * y)) / (1 + t + x + y) +
             (2 * Pth * x * (t + Power(x, 2) + Power(y, 2))) / (1 + t + x + y);
    f0[1] -= 1 - (4. * mu) / R + Pth / (Power(F, 2) * (1 + t + x + y)) + (Pth * S) / (1 + t + x + y) + (2 * Pth * x * (t + 2 * Power(x, 2) + 2 * x * y)) / (1 + t + x + y) +
             (Pth * (4 * x + 2 * y) * (t + Power(x, 2) + Power(y, 2))) / (1 + t + x + y);
}

/* f0_w = dT/dt + u.grad(T) - Q */
static void SourceFunction(f0_lowMach_quadratic_w) {
    f0_w_original(dim, Nf, NfAux, uOff, uOff_x, u, u_t, u_x, aOff, aOff_x, a, a_t, a_x, t, X, numConstants, constants, f0);

    const PetscReal S = constants[0];    // STROUHAL
    const PetscReal Pth = constants[6];  // PTH
    const PetscReal Cp = constants[9];   // CP
    const PetscReal H = constants[4];    // HEATRELEASE

    const PetscReal x = X[0];
    const PetscReal y = X[1];

    f0[0] -= (Cp * Pth * (S + 2 * t + 3 * Power(x, 2) + 2 * x * y + Power(y, 2))) / (1 + t + x + y);
}

/*
  CASE: low mach cubic
  In 2D we use exact solution:

    u = t + x^3 + y^3
    v = t + 2x^3 + 3x^2y
    p = 3/2 x^2 + 3/2 y^2 - 1.125
    T = t + 1/2 x^2 + 1/2 y^2 + 1
    z = {0, 1}

  see docs/content/formulations/lowMachFlow/solutions/LowMach_2D_Cubic_MMS.nb.nb

*/
static PetscErrorCode lowMach_cubic_u(PetscInt Dim, PetscReal time, const PetscReal X[], PetscInt Nf, PetscScalar *u, void *ctx) {
    u[0] = time + X[0] * X[0] * X[0] + X[1] * X[1] * X[1];
    u[1] = time + 2.0 * X[0] * X[0] * X[0] + 3.0 * X[0] * X[0] * X[1];
    return 0;
}
static PetscErrorCode lowMach_cubic_u_t(PetscInt Dim, PetscReal time, const PetscReal X[], PetscInt Nf, PetscScalar *u, void *ctx) {
    u[0] = 1.0;
    u[1] = 1.0;
    return 0;
}

static PetscErrorCode lowMach_cubic_p(PetscInt Dim, PetscReal time, const PetscReal X[], PetscInt Nf, PetscScalar *p, void *ctx) {
    p[0] = 3.0 / 2.0 * X[0] * X[0] + 3.0 / 2.0 * X[1] * X[1] - 1.125;
    return 0;
}

static PetscErrorCode lowMach_cubic_T(PetscInt Dim, PetscReal time, const PetscReal X[], PetscInt Nf, PetscScalar *T, void *ctx) {
    T[0] = time + 0.5 * X[0] * X[0] + 0.5 * X[1] * X[1] + 1.0;
    return 0;
}
static PetscErrorCode lowMach_cubic_T_t(PetscInt Dim, PetscReal time, const PetscReal X[], PetscInt Nf, PetscScalar *T, void *ctx) {
    T[0] = 1.0;
    return 0;
}

static void SourceFunction(f0_lowMach_cubic_q) {
    f0_q_original(dim, Nf, NfAux, uOff, uOff_x, u, u_t, u_x, aOff, aOff_x, a, a_t, a_x, t, X, numConstants, constants, f0);
    const PetscReal S = constants[0];    // STROUHAL
    const PetscReal Pth = constants[6];  // PTH
    const PetscReal x = X[0];
    const PetscReal y = X[1];

    f0[0] -= -((Pth * S) / Power(1 + t + Power(x, 2) / 2. + Power(y, 2) / 2., 2)) - (Pth * y * (t + 2 * Power(x, 3) + 3 * Power(x, 2) * y)) / Power(1 + t + Power(x, 2) / 2. + Power(y, 2) / 2., 2) +
             (6 * Pth * Power(x, 2)) / (1 + t + Power(x, 2) / 2. + Power(y, 2) / 2.) - (Pth * x * (t + Power(x, 3) + Power(y, 3))) / Power(1 + t + Power(x, 2) / 2. + Power(y, 2) / 2., 2);
}

static void SourceFunction(f0_lowMach_cubic_v) {
    f0_v_original(dim, Nf, NfAux, uOff, uOff_x, u, u_t, u_x, aOff, aOff_x, a, a_t, a_x, t, X, numConstants, constants, f0);

    const PetscReal S = constants[0];    // STROUHAL
    const PetscReal Pth = constants[6];  // PTH
    const PetscReal mu = constants[7];   // MU
    const PetscReal R = constants[1];    // REYNOLDS
    const PetscReal F = constants[2];    // FROUDE

    const PetscReal x = X[0];
    const PetscReal y = X[1];

    f0[0] -= 3 * x + (Pth * S) / (1 + t + Power(x, 2) / 2. + Power(y, 2) / 2.) + (3 * Pth * Power(y, 2) * (t + 2 * Power(x, 3) + 3 * Power(x, 2) * y)) / (1 + t + Power(x, 2) / 2. + Power(y, 2) / 2.) +
             (3 * Pth * Power(x, 2) * (t + Power(x, 3) + Power(y, 3))) / (1 + t + Power(x, 2) / 2. + Power(y, 2) / 2.) - (4. * mu * x + 1. * mu * (6 * x + 6 * y)) / R;
    f0[1] -= 3 * y - (1. * mu * (12 * x + 6 * y)) / R + Pth / (Power(F, 2) * (1 + t + Power(x, 2) / 2. + Power(y, 2) / 2.)) + (Pth * S) / (1 + t + Power(x, 2) / 2. + Power(y, 2) / 2.) +
             (3 * Pth * Power(x, 2) * (t + 2 * Power(x, 3) + 3 * Power(x, 2) * y)) / (1 + t + Power(x, 2) / 2. + Power(y, 2) / 2.) +
             (Pth * (6 * Power(x, 2) + 6 * x * y) * (t + Power(x, 3) + Power(y, 3))) / (1 + t + Power(x, 2) / 2. + Power(y, 2) / 2.);
}

/* f0_w = dT/dt + u.grad(T) - Q */
static void SourceFunction(f0_lowMach_cubic_w) {
    f0_w_original(dim, Nf, NfAux, uOff, uOff_x, u, u_t, u_x, aOff, aOff_x, a, a_t, a_x, t, X, numConstants, constants, f0);

    const PetscReal S = constants[0];    // STROUHAL
    const PetscReal Pth = constants[6];  // PTH
    const PetscReal Cp = constants[9];   // CP
    const PetscReal H = constants[4];    // HEATRELEASE
    const PetscReal k = constants[8];    // K
    const PetscReal P = constants[3];    // PECLET

    const PetscReal x = X[0];
    const PetscReal y = X[1];

    f0[0] -= (-2 * k) / P + (Cp * Pth * (S + y * (t + 2 * Power(x, 3) + 3 * Power(x, 2) * y) + x * (t + Power(x, 3) + Power(y, 3)))) / (1 + t + Power(x, 2) / 2. + Power(y, 2) / 2.);
}

/*
  CASE: incompressible quadratic
  In 2D we use exact solution:

    u = t + x^2 + y^2
    v = t + 2x^2 - 2xy
    p = x + y - 1
    T = t + x + y
  so that

    \nabla \cdot u = 2x - 2x = 0

  see docs/content/formulations/incompressibleFlow/solutions/Incompressible_2D_Quadratic_MMS.nb
*/
static PetscErrorCode incompressible_quadratic_u(PetscInt Dim, PetscReal time, const PetscReal *X, PetscInt Nf, PetscScalar *u, void *ctx) {
    u[0] = time + X[0] * X[0] + X[1] * X[1];
    u[1] = time + 2.0 * X[0] * X[0] - 2.0 * X[0] * X[1];
    return 0;
}
static PetscErrorCode incompressible_quadratic_u_t(PetscInt Dim, PetscReal time, const PetscReal *X, PetscInt Nf, PetscScalar *u, void *ctx) {
    u[0] = 1.0;
    u[1] = 1.0;
    return 0;
}

static PetscErrorCode incompressible_quadratic_p(PetscInt Dim, PetscReal time, const PetscReal *X, PetscInt Nf, PetscScalar *p, void *ctx) {
    p[0] = X[0] + X[1] - 1.0;
    return 0;
}

static PetscErrorCode incompressible_quadratic_T(PetscInt Dim, PetscReal time, const PetscReal *X, PetscInt Nf, PetscScalar *T, void *ctx) {
    T[0] = time + X[0] + X[1];
    return 0;
}
static PetscErrorCode incompressible_quadratic_T_t(PetscInt Dim, PetscReal time, const PetscReal *X, PetscInt Nf, PetscScalar *T, void *ctx) {
    T[0] = 1.0;
    return 0;
}

/* f0_v = du/dt - f */
static void SourceFunction(f0_incompressible_quadratic_v) {
    f0_v_original(dim, Nf, NfAux, uOff, uOff_x, u, u_t, u_x, aOff, aOff_x, a, a_t, a_x, t, X, numConstants, constants, f0);
    const PetscReal rho = 1.0;
    const PetscReal S = constants[0];   // STROUHAL
    const PetscReal mu = constants[3];  // MU
    const PetscReal R = constants[1];   // REYNOLDS
    const PetscReal x = X[0];
    const PetscReal y = X[1];

    f0[0] -= 1 - (4. * mu) / R + rho * S + 2 * rho * y * (t + 2 * Power(x, 2) - 2 * x * y) + 2 * rho * x * (t + Power(x, 2) + Power(y, 2));
    f0[1] -= 1 - (4. * mu) / R + rho * S - 2 * rho * x * (t + 2 * Power(x, 2) - 2 * x * y) + rho * (4 * x - 2 * y) * (t + Power(x, 2) + Power(y, 2));
}

/* f0_w = dT/dt + u.grad(T) - Q */
static void SourceFunction(f0_incompressible_quadratic_w) {
    f0_w_original(dim, Nf, NfAux, uOff, uOff_x, u, u_t, u_x, aOff, aOff_x, a, a_t, a_x, t, X, numConstants, constants, f0);

    const PetscReal rho = 1.0;
    const PetscReal S = constants[0];   // STROUHAL
    const PetscReal Cp = constants[5];  // CP
    const PetscReal x = X[0];
    const PetscReal y = X[1];

    f0[0] -= Cp * rho * (S + 2 * t + 3 * Power(x, 2) - 2 * x * y + Power(y, 2));
}

/*
  CASE: incompressible cubic
  In 2D we use exact solution:

    u = t + x^3 + y^3
    v = t + 2x^3 - 3x^2y
    p = 3/2 x^2 + 3/2 y^2 - 1
    T = t + 1/2 x^2 + 1/2 y^2

  so that

    \nabla \cdot u = 3x^2 - 3x^2 = 0

   see docs/content/formulations/incompressibleFlow/solutions/Incompressible_2D_Cubic_MMS.nb.nb
*/
static PetscErrorCode incompressible_cubic_u(PetscInt Dim, PetscReal time, const PetscReal *X, PetscInt Nf, PetscScalar *u, void *ctx) {
    u[0] = time + X[0] * X[0] * X[0] + X[1] * X[1] * X[1];
    u[1] = time + 2.0 * X[0] * X[0] * X[0] - 3.0 * X[0] * X[0] * X[1];
    return 0;
}
static PetscErrorCode incompressible_cubic_u_t(PetscInt Dim, PetscReal time, const PetscReal *X, PetscInt Nf, PetscScalar *u, void *ctx) {
    u[0] = 1.0;
    u[1] = 1.0;
    return 0;
}

static PetscErrorCode incompressible_cubic_p(PetscInt Dim, PetscReal time, const PetscReal *X, PetscInt Nf, PetscScalar *p, void *ctx) {
    p[0] = 3.0 * X[0] * X[0] / 2.0 + 3.0 * X[1] * X[1] / 2.0 - 1.0;
    return 0;
}

static PetscErrorCode incompressible_cubic_T(PetscInt Dim, PetscReal time, const PetscReal *X, PetscInt Nf, PetscScalar *T, void *ctx) {
    T[0] = time + X[0] * X[0] / 2.0 + X[1] * X[1] / 2.0;
    return 0;
}
static PetscErrorCode incompressible_cubic_T_t(PetscInt Dim, PetscReal time, const PetscReal *X, PetscInt Nf, PetscScalar *T, void *ctx) {
    T[0] = 1.0;
    return 0;
}

static void SourceFunction(f0_incompressible_cubic_v) {
    f0_v_original(dim, Nf, NfAux, uOff, uOff_x, u, u_t, u_x, aOff, aOff_x, a, a_t, a_x, t, X, numConstants, constants, f0);

    const PetscReal rho = 1.0;
    const PetscReal S = constants[0];   // STROUHAL
    const PetscReal mu = constants[3];  // MU
    const PetscReal R = constants[1];   // REYNOLDS
    const PetscReal x = X[0];
    const PetscReal y = X[1];

    f0[0] -= rho * S + 3 * x + 3 * rho * Power(y, 2) * (t + 2 * Power(x, 3) - 3 * Power(x, 2) * y) + 3 * rho * Power(x, 2) * (t + Power(x, 3) + Power(y, 3)) -
             (12. * mu * x + 1. * mu * (-6 * x + 6 * y)) / R;
    f0[1] -= rho * S - (1. * mu * (12 * x - 6 * y)) / R + 3 * y - 3 * rho * Power(x, 2) * (t + 2 * Power(x, 3) - 3 * Power(x, 2) * y) +
             rho * (6 * Power(x, 2) - 6 * x * y) * (t + Power(x, 3) + Power(y, 3));
}

static void SourceFunction(f0_incompressible_cubic_w) {
    f0_w_original(dim, Nf, NfAux, uOff, uOff_x, u, u_t, u_x, aOff, aOff_x, a, a_t, a_x, t, X, numConstants, constants, f0);

    const PetscReal rho = 1.0;
    const PetscReal S = constants[0];   // STROUHAL
    const PetscReal Cp = constants[5];  // CP
    const PetscReal k = constants[4];   // K
    const PetscReal P = constants[2];   // PECLET
    const PetscReal x = X[0];
    const PetscReal y = X[1];

    f0[0] -= (-2. * k) / P + Cp * rho * (S + 1. * y * (t + 2 * Power(x, 3) - 3 * Power(x, 2) * y) + 1. * x * (t + Power(x, 3) + Power(y, 3)));
}

/*
  CASE: incompressible cubic-trigonometric
  In 2D we use exact solution:

    u = beta cos t + x^3 + y^3
    v = beta sin t + 2x^3 - 3x^2y
    p = 3/2 x^2 + 3/2 y^2 - 1
    T = beta cos t + 1/2 x^2 + 1/2 y^2
    f = < beta cos t 3x^2         + beta sin t (3y^2 - 1) + 3x^5 + 6x^3y^2 - 6x^2y^3 - \nu(6x + 6y)  + 3x,
          beta cos t (6x^2 - 6xy) - beta sin t (3x^2)     + 3x^4y + 6x^2y^3 - 6xy^4  - \nu(12x - 6y) + 3y>
    Q = beta cos t x + beta sin t (y - 1) + x^4 + 2x^3y - 3x^2y^2 + xy^3 - 2\alpha

  so that

    \nabla \cdot u = 3x^2 - 3x^2 = 0

   see docs/content/formulations/incompressibleFlow/solutions/Incompressible_2D_Cubic-Trigonometric_MMS.nb.nb

*/
static PetscErrorCode incompressible_cubic_trig_u(PetscInt Dim, PetscReal time, const PetscReal *X, PetscInt Nf, PetscScalar *u, void *ctx) {
    const PetscReal beta = 100.0;
    u[0] = beta * Cos(time) + X[0] * X[0] * X[0] + X[1] * X[1] * X[1];
    u[1] = beta * Sin(time) + 2.0 * X[0] * X[0] * X[0] - 3.0 * X[0] * X[0] * X[1];
    return 0;
}
static PetscErrorCode incompressible_cubic_trig_u_t(PetscInt Dim, PetscReal time, const PetscReal *X, PetscInt Nf, PetscScalar *u, void *ctx) {
    const PetscReal beta = 100.0;
    u[0] = -beta * Sin(time);
    u[1] = beta * Cos(time);
    return 0;
}

static PetscErrorCode incompressible_cubic_trig_p(PetscInt Dim, PetscReal time, const PetscReal *X, PetscInt Nf, PetscScalar *p, void *ctx) {
    p[0] = 3.0 * X[0] * X[0] / 2.0 + 3.0 * X[1] * X[1] / 2.0 - 1.0;
    return 0;
}

static PetscErrorCode incompressible_cubic_trig_T(PetscInt Dim, PetscReal time, const PetscReal *X, PetscInt Nf, PetscScalar *T, void *ctx) {
    const PetscReal beta = 100.0;
    T[0] = beta * Cos(time) + X[0] * X[0] / 2.0 + X[1] * X[1] / 2.0;
    return 0;
}
static PetscErrorCode incompressible_cubic_trig_T_t(PetscInt Dim, PetscReal time, const PetscReal *X, PetscInt Nf, PetscScalar *T, void *ctx) {
    const PetscReal beta = 100.0;
    T[0] = -beta * Sin(time);
    return 0;
}

static void SourceFunction(f0_incompressible_cubic_trig_v) {
    f0_v_original(dim, Nf, NfAux, uOff, uOff_x, u, u_t, u_x, aOff, aOff_x, a, a_t, a_x, t, X, numConstants, constants, f0);

    const PetscReal beta = 100.0;
    const PetscReal rho = 1.0;
    const PetscReal S = constants[0];   // STROUHAL
    const PetscReal mu = constants[3];  // MU
    const PetscReal R = constants[1];   // REYNOLDS
    const PetscReal x = X[0];
    const PetscReal y = X[1];

    f0[0] -= 3 * x - (12. * mu * x + 1. * mu * (-6 * x + 6 * y)) / R + 3 * rho * Power(x, 2) * (Power(x, 3) + Power(y, 3) + beta * Cos(t)) - beta * rho * S * Sin(t) +
             3 * rho * Power(y, 2) * (2 * Power(x, 3) - 3 * Power(x, 2) * y + beta * Sin(t));
    f0[1] -= (-1. * mu * (12 * x - 6 * y)) / R + 3 * y + beta * rho * S * Cos(t) + rho * (6 * Power(x, 2) - 6 * x * y) * (Power(x, 3) + Power(y, 3) + beta * Cos(t)) -
             3 * rho * Power(x, 2) * (2 * Power(x, 3) - 3 * Power(x, 2) * y + beta * Sin(t));
}

static void SourceFunction(f0_incompressible_cubic_trig_w) {
    f0_w_original(dim, Nf, NfAux, uOff, uOff_x, u, u_t, u_x, aOff, aOff_x, a, a_t, a_x, t, X, numConstants, constants, f0);

    const PetscReal beta = 100.0;
    const PetscReal rho = 1.0;
    const PetscReal S = constants[0];   // STROUHAL
    const PetscReal Cp = constants[5];  // CP
    const PetscReal k = constants[4];   // K
    const PetscReal P = constants[2];   // PECLET
    const PetscReal x = X[0];
    const PetscReal y = X[1];

    f0[0] -= (-2 * k) / P + Cp * rho * (x * (Power(x, 3) + Power(y, 3) + beta * Cos(t)) - beta * S * Sin(t) + y * (2 * Power(x, 3) - 3 * Power(x, 2) * y + beta * Sin(t)));
}

TEST_P(FEFlowMMSTestFixture, ShouldConvergeToExactSolution) {
    StartWithMPI
        {
            DM dmCreate; /* problem definition */
            TS ts;       /* timestepper */

            PetscReal t;

            // Get the testing param
            auto testingParam = GetParam();

            // initialize petsc and mpi
            PetscInitialize(argc, argv, NULL, help);

            // setup the ts
            TSCreate(PETSC_COMM_WORLD, &ts) >> errorChecker;
            CreateMesh(PETSC_COMM_WORLD, &dmCreate, PETSC_TRUE, 2) >> errorChecker;
            TSSetDM(ts, dmCreate) >> errorChecker;
            TSSetExactFinalTime(ts, TS_EXACTFINALTIME_MATCHSTEP) >> errorChecker;

            // pull the parameters from the petsc options
            auto parameters = std::make_shared<ablate::parameters::PetscOptionParameters>();

            // Create the flow object
            std::shared_ptr<ablate::flow::Flow> flowObject = testingParam.createMethod(std::make_shared<ablate::mesh::DMWrapper>(dmCreate), parameters);

            // Override problem with source terms, boundary, and set the exact solution
            {
                PetscDS prob;
                DMGetDS(flowObject->GetDM(), &prob) >> errorChecker;

                // V, W Test Function
                IntegrandTestFunction tempFunctionPointer;
                if (testingParam.f0_v) {
                    PetscDSGetResidual(prob, VTEST, &f0_v_original, &tempFunctionPointer) >> errorChecker;
                    PetscDSSetResidual(prob, VTEST, testingParam.f0_v, tempFunctionPointer) >> errorChecker;
                }
                if (testingParam.f0_w) {
                    PetscDSGetResidual(prob, WTEST, &f0_w_original, &tempFunctionPointer) >> errorChecker;
                    PetscDSSetResidual(prob, WTEST, testingParam.f0_w, tempFunctionPointer) >> errorChecker;
                }
                if (testingParam.f0_q) {
                    PetscDSGetResidual(prob, QTEST, &f0_q_original, &tempFunctionPointer) >> errorChecker;
                    PetscDSSetResidual(prob, QTEST, testingParam.f0_q, tempFunctionPointer) >> errorChecker;
                }

                /* Setup Boundary Conditions */
                PetscInt id;
                id = 3;
                PetscDSAddBoundary(prob, DM_BC_ESSENTIAL, "top wall velocity", "marker", VEL, 0, NULL, (void (*)(void))testingParam.uExact, (void (*)(void))testingParam.u_tExact, 1, &id, NULL) >> errorChecker;
                id = 1;
                PetscDSAddBoundary(prob, DM_BC_ESSENTIAL, "bottom wall velocity", "marker", VEL, 0, NULL, (void (*)(void))testingParam.uExact, (void (*)(void))testingParam.u_tExact, 1, &id, NULL) >> errorChecker;
                id = 2;
                PetscDSAddBoundary(prob, DM_BC_ESSENTIAL, "right wall velocity", "marker", VEL, 0, NULL, (void (*)(void))testingParam.uExact, (void (*)(void))testingParam.u_tExact, 1, &id, NULL) >> errorChecker;
                id = 4;
                PetscDSAddBoundary(prob, DM_BC_ESSENTIAL, "left wall velocity", "marker", VEL, 0, NULL, (void (*)(void))testingParam.uExact, (void (*)(void))testingParam.u_tExact, 1, &id, NULL) >> errorChecker;
                id = 3;
                PetscDSAddBoundary(prob, DM_BC_ESSENTIAL, "top wall temp", "marker", TEMP, 0, NULL, (void (*)(void))testingParam.TExact, (void (*)(void))testingParam.T_tExact, 1, &id, NULL) >> errorChecker;
                id = 1;
                PetscDSAddBoundary(prob, DM_BC_ESSENTIAL, "bottom wall temp", "marker", TEMP, 0, NULL, (void (*)(void))testingParam.TExact, (void (*)(void))testingParam.T_tExact, 1, &id, NULL) >> errorChecker;
                id = 2;
                PetscDSAddBoundary(prob, DM_BC_ESSENTIAL, "right wall temp", "marker", TEMP, 0, NULL, (void (*)(void))testingParam.TExact, (void (*)(void))testingParam.T_tExact, 1, &id, NULL) >> errorChecker;
                id = 4;
                PetscDSAddBoundary(prob, DM_BC_ESSENTIAL, "left wall temp", "marker", TEMP, 0, NULL, (void (*)(void))testingParam.TExact, (void (*)(void))testingParam.T_tExact, 1, &id, NULL) >> errorChecker;

                // Set the exact solution
                PetscDSSetExactSolution(prob, VEL, testingParam.uExact, NULL) >> errorChecker;
                PetscDSSetExactSolution(prob, PRES, testingParam.pExact, NULL) >> errorChecker;
                PetscDSSetExactSolution(prob, TEMP, testingParam.TExact, NULL) >> errorChecker;
                PetscDSSetExactSolutionTimeDerivative(prob, VEL, testingParam.u_tExact, NULL) >> errorChecker;
                PetscDSSetExactSolutionTimeDerivative(prob, PRES, NULL, NULL) >> errorChecker;
                PetscDSSetExactSolutionTimeDerivative(prob, TEMP, testingParam.T_tExact, NULL) >> errorChecker;
            }

            flowObject->CompleteProblemSetup(ts);

            // Name the flow field
            PetscObjectSetName(((PetscObject)flowObject->GetSolutionVector()), "Numerical Solution") >> errorChecker;
            VecSetOptionsPrefix(flowObject->GetSolutionVector(), "num_sol_") >> errorChecker;

            // Setup the TS
            TSSetFromOptions(ts) >> errorChecker;

            // Set initial conditions from the exact solution
            TSSetComputeInitialCondition(ts, SetInitialConditions) >> errorChecker; /* Must come after SetFromOptions() */
            SetInitialConditions(ts, flowObject->GetSolutionVector()) >> errorChecker;

            TSGetTime(ts, &t) >> errorChecker;
            DMSetOutputSequenceNumber(flowObject->GetDM(), 0, t) >> errorChecker;
            DMTSCheckFromOptions(ts, flowObject->GetSolutionVector()) >> errorChecker;
            TSMonitorSet(ts, MonitorError, NULL, NULL) >> errorChecker;

            // Solve in time
            TSSolve(ts, flowObject->GetSolutionVector()) >> errorChecker;

            // Compare the actual vs expected values
            DMTSCheckFromOptions(ts, flowObject->GetSolutionVector()) >> errorChecker;

            // Cleanup
            DMDestroy(&dmCreate) >> errorChecker;
            TSDestroy(&ts) >> errorChecker;
        }
        exit(PetscFinalize());
    EndWithMPI
}

INSTANTIATE_TEST_SUITE_P(
    FEFlow, FEFlowMMSTestFixture,
    testing::Values(
        (FEFlowMMSParameters){.mpiTestParameter = {.testName = "lowMach 2d quadratic tri_p3_p2_p2",
                                                   .nproc = 1,
                                                   .expectedOutputFile = "outputs/flow/lowMach_2d_tri_p3_p2_p2",
                                                   .arguments = "-dm_plex_separate_marker  -dm_refine 0 "
                                                                "-vel_petscspace_degree 3 -pres_petscspace_degree 2 -temp_petscspace_degree 2 "
                                                                "-dmts_check .001 -ts_max_steps 4 -ts_dt 0.1 -ksp_type dgmres -ksp_gmres_restart 10 "
                                                                "-ksp_rtol 1.0e-9 -ksp_atol 1.0e-12 -ksp_error_if_not_converged -pc_type fieldsplit -pc_fieldsplit_0_fields 0,2 "
                                                                "-pc_fieldsplit_1_fields 1 -pc_fieldsplit_type schur -pc_fieldsplit_schur_factorization_type full "
                                                                "-fieldsplit_0_pc_type lu -fieldsplit_pressure_ksp_rtol 1e-10 -fieldsplit_pressure_ksp_atol 1e-12 -fieldsplit_pressure_pc_type jacobi "
                                                                "-dmts_check -1 -snes_linesearch_type basic "
                                                                "-gravityDirection 1"},
                              .createMethod = [](auto mesh, auto parameters) { return std::make_shared<ablate::flow::LowMachFlow>("testFlow", mesh, parameters); },
                              .uExact = lowMach_quadratic_u,
                              .pExact = lowMach_quadratic_p,
                              .TExact = lowMach_quadratic_T,
                              .u_tExact = lowMach_quadratic_u_t,
                              .T_tExact = lowMach_quadratic_T_t,
                              .f0_v = f0_lowMach_quadratic_v,
                              .f0_w = f0_lowMach_quadratic_w,
                              .f0_q = f0_lowMach_quadratic_q},
        (FEFlowMMSParameters){.mpiTestParameter = {.testName = "lowMach 2d quadratic tri_p3_p2_p2 with real coefficients",
                                                   .nproc = 1,
                                                   .expectedOutputFile = "outputs/flow/lowMach_2d_tri_p3_p2_p2_real_coefficients",
                                                   .arguments = "-dm_plex_separate_marker  -dm_refine 0 "
                                                                "-vel_petscspace_degree 3 -pres_petscspace_degree 2 -temp_petscspace_degree 2 "
                                                                "-dmts_check .001 -ts_max_steps 4 -ts_dt 0.1 -ksp_type dgmres -ksp_gmres_restart 10 "
                                                                "-ksp_rtol 1.0e-9 -ksp_atol 1.0e-12 -ksp_error_if_not_converged -pc_type fieldsplit -pc_fieldsplit_0_fields 0,2 "
                                                                "-pc_fieldsplit_1_fields 1 -pc_fieldsplit_type schur -pc_fieldsplit_schur_factorization_type full "
                                                                "-fieldsplit_0_pc_type lu -fieldsplit_pressure_ksp_rtol 1e-10  -fieldsplit_pressure_ksp_atol 1e-12 -fieldsplit_pressure_pc_type jacobi "
                                                                "-dmts_check -1 -snes_linesearch_type basic "
                                                                "-gravityDirection 1 "
                                                                "-pth 91282.5 -strouhal 0.00242007695844728 -reynolds 23126.2780617827 -froude 0.316227766016838 -peclet 16373.1785965753 "
                                                                "-heatRelease 0.00831162126672484 -gamma 0.285337972166998 -mu 1.1 -k 1.2 -cp 1.3 "},
                              .createMethod = [](auto mesh, auto parameters) { return std::make_shared<ablate::flow::LowMachFlow>("testFlow", mesh, parameters); },
                              .uExact = lowMach_quadratic_u,
                              .pExact = lowMach_quadratic_p,
                              .TExact = lowMach_quadratic_T,
                              .u_tExact = lowMach_quadratic_u_t,
                              .T_tExact = lowMach_quadratic_T_t,
                              .f0_v = f0_lowMach_quadratic_v,
                              .f0_w = f0_lowMach_quadratic_w,
                              .f0_q = f0_lowMach_quadratic_q},
        (FEFlowMMSParameters){.mpiTestParameter = {.testName = "lowMach 2d cubic tri_p3_p2_p2",
                                                   .nproc = 1,
                                                   .expectedOutputFile = "outputs/flow/lowMach_2d_cubic_tri_p3_p2_p2",
                                                   .arguments = "-dm_plex_separate_marker  -dm_refine 0 "
                                                                "-vel_petscspace_degree 3 -pres_petscspace_degree 2 -temp_petscspace_degree 2 "
                                                                "-dmts_check .001 -ts_max_steps 4 -ts_dt 0.1 -ksp_type dgmres -ksp_gmres_restart 10 "
                                                                "-ksp_rtol 1.0e-9 -ksp_atol 1.0e-12 -ksp_error_if_not_converged -pc_type fieldsplit -pc_fieldsplit_0_fields 0,2 "
                                                                "-pc_fieldsplit_1_fields 1 -pc_fieldsplit_type schur -pc_fieldsplit_schur_factorization_type full "
                                                                "-fieldsplit_0_pc_type lu -fieldsplit_pressure_ksp_rtol 1e-10 -fieldsplit_pressure_ksp_atol 1e-12 -fieldsplit_pressure_pc_type jacobi "
                                                                "-dmts_check -1 -snes_linesearch_type basic "
                                                                "-gravityDirection 1 "},
                              .createMethod = [](auto mesh, auto parameters) { return std::make_shared<ablate::flow::LowMachFlow>("testFlow", mesh, parameters); },
                              .uExact = lowMach_cubic_u,
                              .pExact = lowMach_cubic_p,
                              .TExact = lowMach_cubic_T,
                              .u_tExact = lowMach_cubic_u_t,
                              .T_tExact = lowMach_cubic_T_t,
                              .f0_v = f0_lowMach_cubic_v,
                              .f0_w = f0_lowMach_cubic_w,
                              .f0_q = f0_lowMach_cubic_q},
        (FEFlowMMSParameters){.mpiTestParameter = {.testName = "lowMach 2d cubic tri_p3_p2_p2 with real coefficients",
                                                   .nproc = 1,
                                                   .expectedOutputFile = "outputs/flow/lowMach_2d_cubic_tri_p3_p2_p2_real_coefficients",
                                                   .arguments = "-dm_plex_separate_marker  -dm_refine 0 "
                                                                "-vel_petscspace_degree 3 -pres_petscspace_degree 2 -temp_petscspace_degree 2 "
                                                                "-dmts_check .001 -ts_max_steps 4 -ts_dt 0.1 -ksp_type dgmres -ksp_gmres_restart 10 "
                                                                "-ksp_rtol 1.0e-9 -ksp_atol 1.0e-12 -ksp_error_if_not_converged -pc_type fieldsplit -pc_fieldsplit_0_fields 0,2 "
                                                                "-pc_fieldsplit_1_fields 1 -pc_fieldsplit_type schur -pc_fieldsplit_schur_factorization_type full "
                                                                "-fieldsplit_0_pc_type lu -fieldsplit_pressure_ksp_rtol 1e-10 -fieldsplit_pressure_ksp_atol 1e-12 -fieldsplit_pressure_pc_type jacobi "
                                                                "-dmts_check -1 -snes_linesearch_type basic "
                                                                "-gravityDirection 1 "
                                                                "-pth 91282.5 -strouhal 0.00242007695844728 -reynolds 23126.2780617827 -froude 0.316227766016838 -peclet 16373.1785965753 "
                                                                "-heatRelease 0.00831162126672484 -gamma 0.285337972166998 -mu 1.1 -k 1.2 -cp 1.3 "},
                              .createMethod = [](auto mesh, auto parameters) { return std::make_shared<ablate::flow::LowMachFlow>("testFlow", mesh, parameters); },
                              .uExact = lowMach_cubic_u,
                              .pExact = lowMach_cubic_p,
                              .TExact = lowMach_cubic_T,
                              .u_tExact = lowMach_cubic_u_t,
                              .T_tExact = lowMach_cubic_T_t,
                              .f0_v = f0_lowMach_cubic_v,
                              .f0_w = f0_lowMach_cubic_w,
                              .f0_q = f0_lowMach_cubic_q},
        (FEFlowMMSParameters){
            .mpiTestParameter = {.testName = "incompressible 2d quadratic tri_p2_p1_p1",
                                 .nproc = 1,
                                 .expectedOutputFile = "outputs/flow/incompressible_2d_tri_p2_p1_p1",
                                 .arguments = "-dm_plex_separate_marker -dm_refine 0 "
                                              "-vel_petscspace_degree 2 -pres_petscspace_degree 1 -temp_petscspace_degree 1 "
                                              "-dmts_check .001 -ts_max_steps 4 -ts_dt 0.1 "
                                              "-ksp_type fgmres -ksp_gmres_restart 10 -ksp_rtol 1.0e-9 -ksp_error_if_not_converged "
                                              "-pc_type fieldsplit -pc_fieldsplit_0_fields 0,2 -pc_fieldsplit_1_fields 1 -pc_fieldsplit_type schur -pc_fieldsplit_schur_factorization_type full "
                                              "-fieldsplit_0_pc_type lu "
                                              "-fieldsplit_pressure_ksp_rtol 1e-10 -fieldsplit_pressure_pc_type jacobi"},
            .createMethod = [](auto mesh, auto parameters) { return std::make_shared<ablate::flow::IncompressibleFlow>("testFlow", mesh, parameters); },
            .uExact = incompressible_quadratic_u,
            .pExact = incompressible_quadratic_p,
            .TExact = incompressible_quadratic_T,
            .u_tExact = incompressible_quadratic_u_t,
            .T_tExact = incompressible_quadratic_T_t,
            .f0_v = f0_incompressible_quadratic_v,
            .f0_w = f0_incompressible_quadratic_w,
            .f0_q = NULL},
        (FEFlowMMSParameters){
            .mpiTestParameter = {.testName = "incompressible 2d quadratic tri_p2_p1_p1 4 proc",
                                 .nproc = 4,
                                 .expectedOutputFile = "outputs/flow/incompressible_2d_tri_p2_p1_p1_nproc4",
                                 .arguments = "-dm_plex_separate_marker -dm_refine 1 -dm_distribute "
                                              "-vel_petscspace_degree 2 -pres_petscspace_degree 1 -temp_petscspace_degree 1 "
                                              "-dmts_check .001 -ts_max_steps 4 -ts_dt 0.1 "
                                              "-ksp_type fgmres -ksp_gmres_restart 10 -ksp_rtol 1.0e-9 -ksp_error_if_not_converged "
                                              "-pc_type fieldsplit -pc_fieldsplit_0_fields 0,2 -pc_fieldsplit_1_fields 1 -pc_fieldsplit_type schur -pc_fieldsplit_schur_factorization_type full "
                                              "-fieldsplit_0_pc_type lu "
                                              "-fieldsplit_pressure_ksp_rtol 1e-10 -fieldsplit_pressure_pc_type jacobi"},
            .createMethod = [](auto mesh, auto parameters) { return std::make_shared<ablate::flow::IncompressibleFlow>("testFlow", mesh, parameters); },
            .uExact = incompressible_quadratic_u,
            .pExact = incompressible_quadratic_p,
            .TExact = incompressible_quadratic_T,
            .u_tExact = incompressible_quadratic_u_t,
            .T_tExact = incompressible_quadratic_T_t,
            .f0_v = f0_incompressible_quadratic_v,
            .f0_w = f0_incompressible_quadratic_w,
            .f0_q = NULL},
        (FEFlowMMSParameters){
            .mpiTestParameter = {.testName = "incompressible 2d cubic trig tri_p2_p1_p1_tconv",
                                 .nproc = 1,
                                 .expectedOutputFile = "outputs/flow/incompressible_2d_tri_p2_p1_p1_tconv",
                                 .arguments = "-dm_plex_separate_marker -dm_refine 0 "
                                              "-vel_petscspace_degree 2 -pres_petscspace_degree 1 -temp_petscspace_degree 1 "
                                              "-ts_max_steps 4 -ts_dt 0.1 -ts_convergence_estimate -convest_num_refine 1 "
                                              "-snes_error_if_not_converged -snes_convergence_test correct_pressure "
                                              "-ksp_type fgmres -ksp_gmres_restart 10 -ksp_rtol 1.0e-9 -ksp_error_if_not_converged "
                                              "-pc_type fieldsplit -pc_fieldsplit_0_fields 0,2 -pc_fieldsplit_1_fields 1 -pc_fieldsplit_type schur -pc_fieldsplit_schur_factorization_type full "
                                              "-fieldsplit_0_pc_type lu "
                                              "-fieldsplit_pressure_ksp_rtol 1e-10 -fieldsplit_pressure_pc_type jacobi"},
            .createMethod = [](auto mesh, auto parameters) { return std::make_shared<ablate::flow::IncompressibleFlow>("testFlow", mesh, parameters); },
            .uExact = incompressible_cubic_trig_u,
            .pExact = incompressible_cubic_trig_p,
            .TExact = incompressible_cubic_trig_T,
            .u_tExact = incompressible_cubic_trig_u_t,
            .T_tExact = incompressible_cubic_trig_T_t,
            .f0_v = f0_incompressible_cubic_trig_v,
            .f0_w = f0_incompressible_cubic_trig_w,
            .f0_q = NULL},
        (FEFlowMMSParameters){
            .mpiTestParameter = {.testName = "incompressible 2d cubic p2_p1_p1_sconv",
                                 .nproc = 1,
                                 .expectedOutputFile = "outputs/flow/incompressible_2d_tri_p2_p1_p1_sconv",
                                 .arguments = "-dm_plex_separate_marker -dm_refine 0 "
                                              "-vel_petscspace_degree 2 -pres_petscspace_degree 1 -temp_petscspace_degree 1 "
                                              "-ts_max_steps 1 -ts_dt 1e-4 -ts_convergence_estimate -ts_convergence_temporal 0 -convest_num_refine 1 "
                                              "-snes_error_if_not_converged -snes_convergence_test correct_pressure "
                                              "-ksp_type fgmres -ksp_gmres_restart 10 -ksp_rtol 1.0e-9 -ksp_atol 1e-16 -ksp_error_if_not_converged "
                                              "-pc_type fieldsplit -pc_fieldsplit_0_fields 0,2 -pc_fieldsplit_1_fields 1 -pc_fieldsplit_type schur -pc_fieldsplit_schur_factorization_type full "
                                              "-fieldsplit_0_pc_type lu "
                                              "-fieldsplit_pressure_ksp_rtol 1e-10 -fieldsplit_pressure_pc_type jacobi"},
            .createMethod = [](auto mesh, auto parameters) { return std::make_shared<ablate::flow::IncompressibleFlow>("testFlow", mesh, parameters); },
            .uExact = incompressible_cubic_u,
            .pExact = incompressible_cubic_p,
            .TExact = incompressible_cubic_T,
            .u_tExact = incompressible_cubic_u_t,
            .T_tExact = incompressible_cubic_T_t,
            .f0_v = f0_incompressible_cubic_v,
            .f0_w = f0_incompressible_cubic_w,
            .f0_q = NULL},
        (FEFlowMMSParameters){
            .mpiTestParameter = {.testName = "incompressible 2d cubic tri_p3_p2_p2",
                                 .nproc = 1,
                                 .expectedOutputFile = "outputs/flow/incompressible_2d_tri_p3_p2_p2",
                                 .arguments = "-dm_plex_separate_marker -dm_refine 0 "
                                              "-vel_petscspace_degree 3 -pres_petscspace_degree 2 -temp_petscspace_degree 2 "
                                              "-dmts_check .001 -ts_max_steps 4 -ts_dt 0.1 "
                                              "-snes_convergence_test correct_pressure "
                                              "-ksp_type fgmres -ksp_gmres_restart 10 -ksp_rtol 1.0e-9 -ksp_error_if_not_converged "
                                              "-pc_type fieldsplit -pc_fieldsplit_0_fields 0,2 -pc_fieldsplit_1_fields 1 -pc_fieldsplit_type schur -pc_fieldsplit_schur_factorization_type full "
                                              "-fieldsplit_0_pc_type lu "
                                              "-fieldsplit_pressure_ksp_rtol 1e-10 -fieldsplit_pressure_pc_type jacobi"},
            .createMethod = [](auto mesh, auto parameters) { return std::make_shared<ablate::flow::IncompressibleFlow>("testFlow", mesh, parameters); },
            .uExact = incompressible_cubic_u,
            .pExact = incompressible_cubic_p,
            .TExact = incompressible_cubic_T,
            .u_tExact = incompressible_cubic_u_t,
            .T_tExact = incompressible_cubic_T_t,
            .f0_v = f0_incompressible_cubic_v,
            .f0_w = f0_incompressible_cubic_w,
            .f0_q = NULL},
        (FEFlowMMSParameters){
            .mpiTestParameter = {.testName = "incompressible 2d quadratic tri_p2_p1_p1 with real coefficients",
                                 .nproc = 1,
                                 .expectedOutputFile = "outputs/flow/incompressible_2d_tri_p2_p1_p1_real_coefficients",
                                 .arguments = "-dm_plex_separate_marker -dm_refine 0 "
                                              "-vel_petscspace_degree 2 -pres_petscspace_degree 1 -temp_petscspace_degree 1 "
                                              "-dmts_check .001 -ts_max_steps 4 -ts_dt 0.1 "
                                              "-ksp_type fgmres -ksp_gmres_restart 10 -ksp_rtol 1.0e-9 -ksp_error_if_not_converged "
                                              "-pc_type fieldsplit -pc_fieldsplit_0_fields 0,2 -pc_fieldsplit_1_fields 1 -pc_fieldsplit_type schur -pc_fieldsplit_schur_factorization_type full "
                                              "-fieldsplit_0_pc_type lu "
                                              "-fieldsplit_pressure_ksp_rtol 1e-10 -fieldsplit_pressure_pc_type jacobi "
                                              "-strouhal 0.00242007695844728 -reynolds 23126.2780617827  -peclet 16373.1785965753 "
                                              "-mu 1.1 -k 1.2 -cp 1.3 "},
            .createMethod = [](auto mesh, auto parameters) { return std::make_shared<ablate::flow::IncompressibleFlow>("testFlow", mesh, parameters); },
            .uExact = incompressible_quadratic_u,
            .pExact = incompressible_quadratic_p,
            .TExact = incompressible_quadratic_T,
            .u_tExact = incompressible_quadratic_u_t,
            .T_tExact = incompressible_quadratic_T_t,
            .f0_v = f0_incompressible_quadratic_v,
            .f0_w = f0_incompressible_quadratic_w,
            .f0_q = NULL},
        (FEFlowMMSParameters){
            .mpiTestParameter = {.testName = "incompressible 2d cubic tri_p3_p2_p2 with real coefficients",
                                 .nproc = 1,
                                 .expectedOutputFile = "outputs/flow/incompressible_2d_tri_p3_p2_p2_real_coefficients",
                                 .arguments = "-dm_plex_separate_marker -dm_refine 0 "
                                              "-vel_petscspace_degree 3 -pres_petscspace_degree 2 -temp_petscspace_degree 2 "
                                              "-dmts_check .001 -ts_max_steps 4 -ts_dt 0.1 "
                                              "-snes_convergence_test correct_pressure "
                                              "-ksp_type fgmres -ksp_gmres_restart 10 -ksp_rtol 1.0e-9 -ksp_atol 1.0e-12 -ksp_error_if_not_converged "
                                              "-pc_type fieldsplit -pc_fieldsplit_0_fields 0,2 -pc_fieldsplit_1_fields 1 -pc_fieldsplit_type schur -pc_fieldsplit_schur_factorization_type full "
                                              "-fieldsplit_0_pc_type lu "
                                              "-fieldsplit_pressure_ksp_rtol 1e-10  -fieldsplit_pressure_ksp_atol 1E-12 -fieldsplit_pressure_pc_type jacobi "
                                              "-strouhal 0.0024 -reynolds 23126.27 -peclet 16373.178 "
                                              "-mu 1.1 -k 1.2 -cp 1.3 "},
            .createMethod = [](auto mesh, auto parameters) { return std::make_shared<ablate::flow::IncompressibleFlow>("testFlow", mesh, parameters); },
            .uExact = incompressible_cubic_u,
            .pExact = incompressible_cubic_p,
            .TExact = incompressible_cubic_T,
            .u_tExact = incompressible_cubic_u_t,
            .T_tExact = incompressible_cubic_T_t,
            .f0_v = f0_incompressible_cubic_v,
            .f0_w = f0_incompressible_cubic_w,
            .f0_q = NULL}),
    [](const testing::TestParamInfo<FEFlowMMSParameters> &info) { return info.param.mpiTestParameter.getTestName(); });