#include "boxMesh.hpp"
#include <stdexcept>
#include <utilities/mpiError.hpp>
#include "../utilities/petscError.hpp"
#include "parser/registrar.hpp"

ablate::mesh::BoxMesh::BoxMesh(std::string name, std::map<std::string, std::string> arguments, std::vector<int> faces, std::vector<double> lower, std::vector<double> upper,
                               std::vector<std::string> boundary, bool simplex)
    : Mesh(name, Merge(arguments, {{"dm_distribute", "true"}})) {
    PetscInt dimensions = faces.size();
    if ((dimensions != lower.size()) || (dimensions != upper.size())) {
        throw std::runtime_error("BoxMesh Error: The faces, lower, and upper vectors must all be the same dimension.");
    }

    std::vector<DMBoundaryType> boundaryTypes(dimensions, DM_BOUNDARY_NONE);
    for (auto d = 0; PetscMin(d < dimensions, boundary.size()); d++) {
        PetscBool found;
        PetscEnum index;
        PetscEnumFind(DMBoundaryTypes, boundary[d].c_str(), &index, &found) >> checkError;

        if (found) {
            boundaryTypes[d] = (DMBoundaryType)index;
        } else {
            throw std::invalid_argument("unable to find boundary type " + boundary[d]);
        }
    }

    DMPlexCreateBoxMesh(PETSC_COMM_WORLD, dimensions, simplex ? PETSC_TRUE : PETSC_FALSE, &faces[0], &lower[0], &upper[0], &boundaryTypes[0], PETSC_TRUE, &dm) >> checkError;
    DMSetOptionsPrefix(dm, name.c_str()) >> checkError;
    DMSetFromOptions(dm) >> checkError;

    IS globalCellNumbers;
    DMPlexGetCellNumbering(dm, &globalCellNumbers) >> checkError;
    PetscInt size;
    ISGetLocalSize(globalCellNumbers, &size) >> checkError;
    if (size == 0) {
        int rank;
        MPI_Comm_rank(PETSC_COMM_WORLD, &rank) >> checkMpiError;
        throw std::runtime_error("BoxMesh Error: Rank " + std::to_string(rank) + " distribution resulted in no cells.  Increase the number of cells in each direction.");
    }
}
ablate::mesh::BoxMesh::~BoxMesh() {
    if (dm) {
        DMDestroy(&dm);
    }
}

REGISTER(ablate::mesh::Mesh, ablate::mesh::BoxMesh, "a simple uniform box", ARG(std::string, "name", "the name of the mesh/domain"),
         ARG(std::map<std::string TMP_COMMA std::string>, "arguments", "arguments to be passed to petsc"), ARG(std::vector<int>, "faces", "the number of faces in each direction for the mesh"),
         ARG(std::vector<double>, "lower", "the lower bound for the mesh"), ARG(std::vector<double>, "upper", "the upper bound for the mesh"),
         OPT(std::vector<std::string>, "boundary", "the boundary type in each direction (NONE, PERIODIC)"), OPT(bool, "simplex", "if the elements are simplex"));
