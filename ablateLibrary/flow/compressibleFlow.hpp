#ifndef ABLATELIBRARY_COMPRESSIBLEFLOW_H
#define ABLATELIBRARY_COMPRESSIBLEFLOW_H

#include <petsc.h>
#include <eos/eos.hpp>
#include <string>
#include "flow.hpp"
#include "mesh/mesh.hpp"
#include "parameters/parameters.hpp"
#include "fluxDifferencer.h"
#include "compressibleFlow.h"

namespace ablate::flow {
class CompressibleFlow : public Flow {
   private:
    std::shared_ptr<eos::EOS> eos;

    FlowData_CompressibleFlow compressibleFlowData;

    // functions to update each aux field
    FVAuxFieldUpdateFunction auxFieldUpdateFunctions[TOTAL_COMPRESSIBLE_AUX_COMPONENTS];

    // static function to update the flowfield
    static void ComputeTimeStep(TS, Flow&);

   public:
    CompressibleFlow(std::string name, std::shared_ptr<mesh::Mesh> mesh,  std::shared_ptr<eos::EOS> eos, std::shared_ptr<parameters::Parameters> parameters, std::shared_ptr<parameters::Parameters> options = {},
                     std::vector<std::shared_ptr<FlowFieldSolution>> initialization = {}, std::vector<std::shared_ptr<BoundaryCondition>> boundaryConditions = {});

    void CompleteProblemSetup(TS ts) override;
    void CompleteFlowInitialization(DM, Vec) override;
    static PetscErrorCode CompressibleFlowRHSFunctionLocal(DM dm, PetscReal time, Vec locXVec, Vec globFVec, void *ctx);
};
}  // namespace ablate::flow

#endif  // ABLATELIBRARY_COMPRESSIBLEFLOW_H
