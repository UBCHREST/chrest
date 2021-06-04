#if !defined(compressibleFlow_h)
#define compressibleFlow_h
#include <petsc.h>
#include "fluxDifferencer.h"
#include "eos.h"

typedef enum {RHO, RHOE, RHOU, RHOV, RHOW, TOTAL_COMPRESSIBLE_FLOW_COMPONENTS} CompressibleFlowComponents;
typedef enum {T, VEL, TOTAL_COMPRESSIBLE_AUX_COMPONENTS} CompressibleAuxComponents;

struct _FlowData_CompressibleFlow{
    PetscReal cfl;
    PetscReal k;/*thermal conductivity*/
    PetscReal mu;/*dynamic viscosity*/
    FluxDifferencerFunction fluxDifferencer;
    PetscBool automaticTimeStepCalculator;
    EOSData eos;
} ;

typedef struct _FlowData_CompressibleFlow* FlowData_CompressibleFlow;

typedef PetscErrorCode (*FVAuxFieldUpdateFunction)(FlowData_CompressibleFlow flowData, PetscReal time, PetscInt dim, const PetscFVCellGeom *cellGeom, const PetscScalar* conservedValues, PetscScalar* auxField);

PETSC_EXTERN PetscErrorCode FVFlowUpdateAuxFieldsFV(DM dm, DM auxDM, PetscReal time, Vec locXVec, Vec locAuxField, PetscInt numberUpdateFunctions, FVAuxFieldUpdateFunction* updateFunctions, FlowData_CompressibleFlow data);
PETSC_EXTERN PetscErrorCode CompressibleFlowDiffusionSourceRHSFunctionLocal(DM dm, DM auxDM, PetscReal time, Vec locXVec, Vec locAuxVec, Vec globFVec, FlowData_CompressibleFlow flowParameters );
PETSC_EXTERN void CompressibleFlowComputeEulerFlux(PetscInt dim, PetscInt Nf, const PetscReal *qp, const PetscReal *area, const PetscReal *xL, const PetscReal *xR, PetscInt numConstants, const PetscScalar constants[], PetscReal *flux, void* ctx);
PETSC_EXTERN PetscErrorCode CompressibleFlowComputeStressTensor(PetscInt dim, PetscReal mu, const PetscReal* gradVelL, const PetscReal * gradVelR, PetscReal* tau);

#endif