#include "compressibleFlow.hpp"
#include <flow/processes/eulerAdvection.hpp>
#include <flow/processes/eulerDiffusion.hpp>
#include <utilities/mpiError.hpp>

ablate::flow::CompressibleFlow::CompressibleFlow(std::string name, std::shared_ptr<mesh::Mesh> mesh, std::shared_ptr<eos::EOS> eosIn, std::shared_ptr<parameters::Parameters> parameters,
                                                 std::shared_ptr<fluxCalculator::FluxCalculator> fluxCalculatorIn, std::shared_ptr<parameters::Parameters> options,
                                                 std::vector<std::shared_ptr<mathFunctions::FieldSolution>> initialization,
                                                 std::vector<std::shared_ptr<boundaryConditions::BoundaryCondition>> boundaryConditions,
                                                 std::vector<std::shared_ptr<mathFunctions::FieldSolution>> exactSolutions)
    : FVFlow(name, mesh, parameters,
             {{.fieldName = "euler", .fieldPrefix = "euler", .components = 2 + mesh->GetDimensions(), .fieldType = FieldType::FV},
              {
                  .fieldName = "densityYi",
                  .fieldPrefix = "densityYi",
                  .components = (PetscInt)eosIn->GetSpecies().size(),
                  .fieldType = FieldType::FV,
                  .componentNames = eosIn->GetSpecies(),
              },
              {.solutionField = false, .fieldName = "T", .fieldPrefix = "T", .components = 1, .fieldType = FieldType::FV},
              {.solutionField = false, .fieldName = "vel", .fieldPrefix = "vel", .components = mesh->GetDimensions(), .fieldType = FieldType::FV}},
             {
                 // create assumed processes for compressible flow
                 std::make_shared<ablate::flow::processes::EulerAdvection>(parameters, eosIn, fluxCalculatorIn),
                 std::make_shared<ablate::flow::processes::EulerDiffusion>(parameters, eosIn),
             },
             options, initialization, boundaryConditions, {}, exactSolutions) {}

#include "parser/registrar.hpp"
REGISTER(ablate::flow::Flow, ablate::flow::CompressibleFlow, "compressible finite volume flow", ARG(std::string, "name", "the name of the flow field"),
         ARG(ablate::mesh::Mesh, "mesh", "the  mesh and discretization"), ARG(ablate::eos::EOS, "eos", "the equation of state used to describe the flow"),
         ARG(ablate::parameters::Parameters, "parameters", "the compressible flow parameters cfl, gamma, etc."),
         OPT(ablate::flow::fluxCalculator::FluxCalculator, "fluxCalculator", "the flux calculators (defaults to AUSM)"), OPT(ablate::parameters::Parameters, "options", "the options passed to PETSc"),
         OPT(std::vector<mathFunctions::FieldSolution>, "initialization", "the flow field initialization"),
         OPT(std::vector<flow::boundaryConditions::BoundaryCondition>, "boundaryConditions", "the boundary conditions for the flow field"),
         OPT(std::vector<mathFunctions::FieldSolution>, "exactSolution", "optional exact solutions that can be used for error calculations"));