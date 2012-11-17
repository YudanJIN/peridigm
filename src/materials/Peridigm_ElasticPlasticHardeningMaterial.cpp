/*! \file Peridigm_ElasticPlasticHardeningMaterial.cpp */

//@HEADER
// ************************************************************************
//
//                             Peridigm
//                 Copyright (2011) Sandia Corporation
//
// Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions?
// David J. Littlewood   djlittl@sandia.gov
// John A. Mitchell      jamitch@sandia.gov
// Michael L. Parks      mlparks@sandia.gov
// Stewart A. Silling    sasilli@sandia.gov
//
// ************************************************************************
//@HEADER

#include "Peridigm_ElasticPlasticMaterial.hpp"
#include "Peridigm_ElasticPlasticHardeningMaterial.hpp"
#include "Peridigm_Field.hpp"
#include "elastic_plastic.h"
#include "elastic_plastic_hardening.h"
#include "material_utilities.h"
#include <Teuchos_Assert.hpp>
#include <Epetra_SerialComm.h>
#include <Epetra_Vector.h>
#include <Sacado.hpp>
#include <limits>
#include <vector>

using namespace std;

PeridigmNS::ElasticPlasticHardeningMaterial::ElasticPlasticHardeningMaterial(const Teuchos::ParameterList & params)
  : Material(params),
    m_applyShearCorrectionFactor(false), m_disablePlasticity(false), m_applyAutomaticDifferentiationJacobian(false),
    volumeFieldId(-1), damageFieldId(-1), weightedVolumeFieldId(-1), dilatationFieldId(-1), modelCoordinatesFieldId(-1),
    coordinatesFieldId(-1), forceDensityFieldId(-1), bondDamageFieldId(-1), deviatoricPlasticExtensionFieldId(-1),
    lambdaFieldId(-1), surfaceCorrectionFactorFieldId(-1)
{
  //! \todo Add meaningful asserts on material properties.
  m_bulkModulus = params.get<double>("Bulk Modulus");
  m_shearModulus = params.get<double>("Shear Modulus");
  m_horizon = params.get<double>("Horizon");
  m_density = params.get<double>("Density");
  m_yieldStress = params.get<double>("Yield Stress");
  m_hardeningModulus = params.get<double>("Hardening Modulus");
  if(params.isParameter("Apply Shear Correction Factor"))
    m_applyShearCorrectionFactor = params.get<bool>("Apply Shear Correction Factor");
  if(params.isParameter("Disable Plasticity"))
    m_disablePlasticity = params.get<bool>("Disable Plasticity");
  if(params.isParameter("Apply Automatic Differentiation Jacobian"))
    m_applyAutomaticDifferentiationJacobian = params.get<bool>("Apply Automatic Differentiation Jacobian");

  if(m_disablePlasticity)
    m_yieldStress = std::numeric_limits<double>::max();

  // Set up vector of variable specs
  m_variableSpecs = Teuchos::rcp(new std::vector<Field_NS::FieldSpec>);
  m_variableSpecs->push_back(Field_NS::VOLUME);
  m_variableSpecs->push_back(Field_NS::DAMAGE);
  m_variableSpecs->push_back(Field_NS::WEIGHTED_VOLUME);
  m_variableSpecs->push_back(Field_NS::DILATATION);
  m_variableSpecs->push_back(Field_NS::COORD3D);
  m_variableSpecs->push_back(Field_NS::CURCOORD3D);
  m_variableSpecs->push_back(Field_NS::FORCE_DENSITY3D);
  m_variableSpecs->push_back(Field_NS::DEVIATORIC_PLASTIC_EXTENSION);
  m_variableSpecs->push_back(Field_NS::LAMBDA);
  m_variableSpecs->push_back(Field_NS::BOND_DAMAGE);
  m_variableSpecs->push_back(Field_NS::SHEAR_CORRECTION_FACTOR);

  PeridigmNS::FieldManager& fieldManager = PeridigmNS::FieldManager::self();

  volumeFieldId                     = fieldManager.getFieldId(PeridigmNS::PeridigmField::ELEMENT, PeridigmNS::PeridigmField::SCALAR, PeridigmNS::PeridigmField::CONSTANT, "Volume");
  damageFieldId                     = fieldManager.getFieldId(PeridigmNS::PeridigmField::ELEMENT, PeridigmNS::PeridigmField::SCALAR, PeridigmNS::PeridigmField::TWO_STEP, "Damage");
  weightedVolumeFieldId             = fieldManager.getFieldId(PeridigmNS::PeridigmField::ELEMENT, PeridigmNS::PeridigmField::SCALAR, PeridigmNS::PeridigmField::CONSTANT, "Weighted_Volume");
  dilatationFieldId                 = fieldManager.getFieldId(PeridigmNS::PeridigmField::ELEMENT, PeridigmNS::PeridigmField::SCALAR, PeridigmNS::PeridigmField::TWO_STEP, "Dilatation");
  modelCoordinatesFieldId           = fieldManager.getFieldId(PeridigmNS::PeridigmField::NODE,    PeridigmNS::PeridigmField::VECTOR, PeridigmNS::PeridigmField::CONSTANT, "Model_Coordinates");
  coordinatesFieldId                = fieldManager.getFieldId(PeridigmNS::PeridigmField::NODE,    PeridigmNS::PeridigmField::VECTOR, PeridigmNS::PeridigmField::TWO_STEP, "Coordinates");
  forceDensityFieldId               = fieldManager.getFieldId(PeridigmNS::PeridigmField::NODE,    PeridigmNS::PeridigmField::VECTOR, PeridigmNS::PeridigmField::TWO_STEP, "Force_Density");
  bondDamageFieldId                 = fieldManager.getFieldId(PeridigmNS::PeridigmField::BOND,    PeridigmNS::PeridigmField::SCALAR, PeridigmNS::PeridigmField::TWO_STEP, "Bond_Damage");
  deviatoricPlasticExtensionFieldId = fieldManager.getFieldId(PeridigmNS::PeridigmField::BOND,    PeridigmNS::PeridigmField::SCALAR, PeridigmNS::PeridigmField::TWO_STEP, "Deviatoric_Plastic_Extension");
  lambdaFieldId                     = fieldManager.getFieldId(PeridigmNS::PeridigmField::ELEMENT, PeridigmNS::PeridigmField::SCALAR, PeridigmNS::PeridigmField::TWO_STEP, "Lambda");
  surfaceCorrectionFactorFieldId    = fieldManager.getFieldId(PeridigmNS::PeridigmField::ELEMENT, PeridigmNS::PeridigmField::SCALAR, PeridigmNS::PeridigmField::CONSTANT, "Surface_Correction_Factor");
}

PeridigmNS::ElasticPlasticHardeningMaterial::~ElasticPlasticHardeningMaterial()
{
}

void PeridigmNS::ElasticPlasticHardeningMaterial::initialize(const double dt,
                                                             const int numOwnedPoints,
                                                             const int* ownedIDs,
                                                             const int* neighborhoodList,
                                                             PeridigmNS::DataManager& dataManager) const
{
  double *xOverlap, *yOverlapScratch, *cellVolumeOverlap, *weightedVolume, *surfaceCorrectionFactor;
  // dataManager.getData(Field_NS::COORD3D, Field_ENUM::STEP_NONE)->ExtractView(&xOverlap);
  // dataManager.getData(Field_NS::CURCOORD3D, Field_ENUM::STEP_NP1)->ExtractView(&yOverlapScratch);
  // dataManager.getData(Field_NS::VOLUME, Field_ENUM::STEP_NONE)->ExtractView(&cellVolumeOverlap);
  // dataManager.getData(Field_NS::WEIGHTED_VOLUME, Field_ENUM::STEP_NONE)->ExtractView(&weightedVolume);
  // dataManager.getData(Field_NS::SHEAR_CORRECTION_FACTOR, Field_ENUM::STEP_NONE)->ExtractView(&surfaceCorrectionFactor);

  dataManager.getData(modelCoordinatesFieldId, Field_ENUM::STEP_NONE)->ExtractView(&xOverlap);
  dataManager.getData(coordinatesFieldId, Field_ENUM::STEP_NP1)->ExtractView(&yOverlapScratch);
  dataManager.getData(volumeFieldId, Field_ENUM::STEP_NONE)->ExtractView(&cellVolumeOverlap);
  dataManager.getData(weightedVolumeFieldId, Field_ENUM::STEP_NONE)->ExtractView(&weightedVolume);
  dataManager.getData(surfaceCorrectionFactorFieldId, Field_ENUM::STEP_NONE)->ExtractView(&surfaceCorrectionFactor);
  
  MATERIAL_EVALUATION::computeWeightedVolume(xOverlap,cellVolumeOverlap,weightedVolume,numOwnedPoints,neighborhoodList);

  dataManager.getData(surfaceCorrectionFactorFieldId, Field_ENUM::STEP_NONE)->PutScalar(1.0);
  int lengthYOverlap = dataManager.getData(coordinatesFieldId, Field_ENUM::STEP_NP1)->MyLength();
  if(m_applyShearCorrectionFactor)
    MATERIAL_EVALUATION::computeShearCorrectionFactor(numOwnedPoints,lengthYOverlap,xOverlap,yOverlapScratch,cellVolumeOverlap,weightedVolume,neighborhoodList,m_horizon,surfaceCorrectionFactor);
  
  // \todo Move this to shear correction factor routine.
  for(double *scf=surfaceCorrectionFactor; scf!=surfaceCorrectionFactor+numOwnedPoints;scf++)
    *scf = 1.0/(*scf);
}

void
PeridigmNS::ElasticPlasticHardeningMaterial::computeForce(const double dt,
                                                          const int numOwnedPoints,
                                                          const int* ownedIDs,
                                                          const int* neighborhoodList,
                                                          PeridigmNS::DataManager& dataManager) const
{
  double *x, *y, *volume, *dilatation, *weightedVolume, *bondDamage, *edpN, *edpNP1, *lambdaN, *lambdaNP1, *force, *ownedShearCorrectionFactor;
  // dataManager.getData(Field_NS::COORD3D, Field_ENUM::STEP_NONE)->ExtractView(&x);
  // dataManager.getData(Field_NS::CURCOORD3D, Field_ENUM::STEP_NP1)->ExtractView(&y);
  // dataManager.getData(Field_NS::VOLUME, Field_ENUM::STEP_NONE)->ExtractView(&volume);
  // dataManager.getData(Field_NS::DILATATION, Field_ENUM::STEP_NP1)->ExtractView(&dilatation);
  // dataManager.getData(Field_NS::WEIGHTED_VOLUME, Field_ENUM::STEP_NONE)->ExtractView(&weightedVolume);
  // dataManager.getData(Field_NS::BOND_DAMAGE, Field_ENUM::STEP_NP1)->ExtractView(&bondDamage);
  // dataManager.getData(Field_NS::DEVIATORIC_PLASTIC_EXTENSION, Field_ENUM::STEP_N)->ExtractView(&edpN);
  // dataManager.getData(Field_NS::DEVIATORIC_PLASTIC_EXTENSION, Field_ENUM::STEP_NP1)->ExtractView(&edpNP1);
  // dataManager.getData(Field_NS::LAMBDA, Field_ENUM::STEP_N)->ExtractView(&lambdaN);
  // dataManager.getData(Field_NS::LAMBDA, Field_ENUM::STEP_NP1)->ExtractView(&lambdaNP1);
  // dataManager.getData(Field_NS::FORCE_DENSITY3D, Field_ENUM::STEP_NP1)->ExtractView(&force);
  // dataManager.getData(Field_NS::SHEAR_CORRECTION_FACTOR, Field_ENUM::STEP_NONE)->ExtractView(&ownedShearCorrectionFactor);

  dataManager.getData(modelCoordinatesFieldId, Field_ENUM::STEP_NONE)->ExtractView(&x);
  dataManager.getData(coordinatesFieldId, Field_ENUM::STEP_NP1)->ExtractView(&y);
  dataManager.getData(volumeFieldId, Field_ENUM::STEP_NONE)->ExtractView(&volume);
  dataManager.getData(dilatationFieldId, Field_ENUM::STEP_NP1)->ExtractView(&dilatation);
  dataManager.getData(weightedVolumeFieldId, Field_ENUM::STEP_NONE)->ExtractView(&weightedVolume);
  dataManager.getData(bondDamageFieldId, Field_ENUM::STEP_NP1)->ExtractView(&bondDamage);
  dataManager.getData(deviatoricPlasticExtensionFieldId, Field_ENUM::STEP_N)->ExtractView(&edpN);
  dataManager.getData(deviatoricPlasticExtensionFieldId, Field_ENUM::STEP_NP1)->ExtractView(&edpNP1);
  dataManager.getData(lambdaFieldId, Field_ENUM::STEP_N)->ExtractView(&lambdaN);
  dataManager.getData(lambdaFieldId, Field_ENUM::STEP_NP1)->ExtractView(&lambdaNP1);
  dataManager.getData(forceDensityFieldId, Field_ENUM::STEP_NP1)->ExtractView(&force);
  dataManager.getData(surfaceCorrectionFactorFieldId, Field_ENUM::STEP_NONE)->ExtractView(&ownedShearCorrectionFactor);

  // Zero out the force
  dataManager.getData(forceDensityFieldId, Field_ENUM::STEP_NP1)->PutScalar(0.0);

  MATERIAL_EVALUATION::computeDilatation(x,y,weightedVolume,volume,bondDamage,dilatation,neighborhoodList,numOwnedPoints);
  MATERIAL_EVALUATION::computeInternalForceIsotropicHardeningPlastic(x,
                                                                     y,
                                                                     weightedVolume,
                                                                     volume,
                                                                     dilatation,
                                                                     bondDamage,
                                                                     ownedShearCorrectionFactor,
                                                                     edpN,
                                                                     edpNP1,
                                                                     lambdaN,
                                                                     lambdaNP1,
                                                                     force,
                                                                     neighborhoodList,
                                                                     numOwnedPoints,
                                                                     m_bulkModulus,
                                                                     m_shearModulus,
                                                                     m_horizon,
                                                                     m_yieldStress,
                                                                     m_hardeningModulus);
}

void
PeridigmNS::ElasticPlasticHardeningMaterial::computeJacobian(const double dt,
                                                             const int numOwnedPoints,
                                                             const int* ownedIDs,
                                                             const int* neighborhoodList,
                                                             PeridigmNS::DataManager& dataManager,
                                                             PeridigmNS::SerialMatrix& jacobian) const
{
  if(m_applyAutomaticDifferentiationJacobian){
    // Compute the Jacobian via automatic differentiation
    computeAutomaticDifferentiationJacobian(dt, numOwnedPoints, ownedIDs, neighborhoodList, dataManager, jacobian);  
  }
  else{
    // Call the base class function, which computes the Jacobian by finite difference
    PeridigmNS::Material::computeJacobian(dt, numOwnedPoints, ownedIDs, neighborhoodList, dataManager, jacobian);
  }
}

void
PeridigmNS::ElasticPlasticHardeningMaterial::computeAutomaticDifferentiationJacobian(const double dt,
                                                                                     const int numOwnedPoints,
                                                                                     const int* ownedIDs,
                                                                                     const int* neighborhoodList,
                                                                                     PeridigmNS::DataManager& dataManager,
                                                                                     PeridigmNS::SerialMatrix& jacobian) const
{
  // Compute contributions to the tangent matrix on an element-by-element basis

  // To reduce memory re-allocation, use static variable to store Fad types for
  // current coordinates (independent variables).
  static vector<Sacado::Fad::DFad<double> > y_AD;

  // Loop over all points.
  int neighborhoodListIndex = 0;
  for(int iID=0 ; iID<numOwnedPoints ; ++iID){

    // Create a temporary neighborhood consisting of a single point and its neighbors.
    // The temporary neighborhood is sorted by global ID to somewhat increase the chance
    // that the downstream Epetra_CrsMatrix::SumIntoMyValues() calls will be as efficient
    // as possible.
    int numNeighbors = neighborhoodList[neighborhoodListIndex++];
    int numEntries = numNeighbors+1;
    int numDof = 3*numEntries;
    vector<int> tempMyGlobalIDs(numEntries);
    // Create a placeholder that will appear at the beginning of the sorted list.
    tempMyGlobalIDs[0] = -1;
    vector<int> tempNeighborhoodList(numEntries); 
    tempNeighborhoodList[0] = numNeighbors;
    for(int iNID=0 ; iNID<numNeighbors ; ++iNID){
      int neighborID = neighborhoodList[neighborhoodListIndex++];
      tempMyGlobalIDs[iNID+1] = dataManager.getOverlapScalarPointMap()->GID(neighborID);
      tempNeighborhoodList[iNID+1] = iNID+1;
    }
    sort(tempMyGlobalIDs.begin(), tempMyGlobalIDs.end());
    // Put the node at the center of the neighborhood at the beginning of the list.
    tempMyGlobalIDs[0] = dataManager.getOwnedScalarPointMap()->GID(iID);

    Epetra_SerialComm serialComm;
    Teuchos::RCP<Epetra_BlockMap> tempOneDimensionalMap = Teuchos::rcp(new Epetra_BlockMap(numEntries, numEntries, &tempMyGlobalIDs[0], 1, 0, serialComm));
    Teuchos::RCP<Epetra_BlockMap> tempThreeDimensionalMap = Teuchos::rcp(new Epetra_BlockMap(numEntries, numEntries, &tempMyGlobalIDs[0], 3, 0, serialComm));
    Teuchos::RCP<Epetra_BlockMap> tempBondMap = Teuchos::rcp(new Epetra_BlockMap(1, 1, &tempMyGlobalIDs[0], numNeighbors, 0, serialComm));

    // Create a temporary DataManager containing data for this point and its neighborhood.
    PeridigmNS::DataManager tempDataManager;
    tempDataManager.setMaps(Teuchos::RCP<const Epetra_BlockMap>(),
                            tempOneDimensionalMap,
                            Teuchos::RCP<const Epetra_BlockMap>(),
                            tempThreeDimensionalMap,
                            tempBondMap);

    // The temporary data manager will have the same field specs and data as the real data manager.
    Teuchos::RCP< std::vector<Field_NS::FieldSpec> > fieldSpecs = dataManager.getFieldSpecs();
    tempDataManager.allocateData(fieldSpecs);
    tempDataManager.copyLocallyOwnedDataFromDataManager(dataManager);

    // Set up numOwnedPoints and ownedIDs.
    // There is only one owned ID, and it has local ID zero in the tempDataManager.
    int tempNumOwnedPoints = 1;
    vector<int> tempOwnedIDs(tempNumOwnedPoints);
    tempOwnedIDs[0] = 0;

    // Use the scratchMatrix as sub-matrix for storing tangent values prior to loading them into the global tangent matrix.
    // Resize scratchMatrix if necessary
    if(scratchMatrix.Dimension() < numDof)
      scratchMatrix.Resize(numDof);

    // Create a list of global indices for the rows/columns in the scratch matrix.
    vector<int> globalIndices(numDof);
    for(int i=0 ; i<numEntries ; ++i){
      int globalID = tempOneDimensionalMap->GID(i);
      for(int j=0 ; j<3 ; ++j)
        globalIndices[3*i+j] = 3*globalID+j;
    }

    // Extract pointers to the underlying data in the constitutiveData array.
    double *x, *y, *cellVolume, *weightedVolume, *damage, *bondDamage, *edpN, *lambdaN, *ownedShearCorrectionFactor;
    // tempDataManager.getData(Field_NS::COORD3D, Field_ENUM::STEP_NONE)->ExtractView(&x);
    // tempDataManager.getData(Field_NS::CURCOORD3D, Field_ENUM::STEP_NP1)->ExtractView(&y);
    // tempDataManager.getData(Field_NS::VOLUME, Field_ENUM::STEP_NONE)->ExtractView(&cellVolume);
    // tempDataManager.getData(Field_NS::WEIGHTED_VOLUME, Field_ENUM::STEP_NONE)->ExtractView(&weightedVolume);
    // tempDataManager.getData(Field_NS::DAMAGE, Field_ENUM::STEP_NP1)->ExtractView(&damage);
    // tempDataManager.getData(Field_NS::BOND_DAMAGE, Field_ENUM::STEP_NP1)->ExtractView(&bondDamage);
    // tempDataManager.getData(Field_NS::DEVIATORIC_PLASTIC_EXTENSION, Field_ENUM::STEP_N)->ExtractView(&edpN);
    // tempDataManager.getData(Field_NS::LAMBDA, Field_ENUM::STEP_N)->ExtractView(&lambdaN);
    // tempDataManager.getData(Field_NS::SHEAR_CORRECTION_FACTOR, Field_ENUM::STEP_NONE)->ExtractView(&ownedShearCorrectionFactor);

    tempDataManager.getData(modelCoordinatesFieldId, Field_ENUM::STEP_NONE)->ExtractView(&x);
    tempDataManager.getData(coordinatesFieldId, Field_ENUM::STEP_NP1)->ExtractView(&y);
    tempDataManager.getData(volumeFieldId, Field_ENUM::STEP_NONE)->ExtractView(&cellVolume);
    tempDataManager.getData(weightedVolumeFieldId, Field_ENUM::STEP_NONE)->ExtractView(&weightedVolume);
    tempDataManager.getData(damageFieldId, Field_ENUM::STEP_NP1)->ExtractView(&damage);
    tempDataManager.getData(bondDamageFieldId, Field_ENUM::STEP_NP1)->ExtractView(&bondDamage);
    tempDataManager.getData(deviatoricPlasticExtensionFieldId, Field_ENUM::STEP_N)->ExtractView(&edpN);
    tempDataManager.getData(lambdaFieldId, Field_ENUM::STEP_N)->ExtractView(&lambdaN);
    tempDataManager.getData(surfaceCorrectionFactorFieldId, Field_ENUM::STEP_NONE)->ExtractView(&ownedShearCorrectionFactor);

    // Create arrays of Fad objects for the current coordinates, dilatation, and force density
    // Modify the existing vector of Fad objects for the current coordinates
    if((int)y_AD.size() < numDof)
      y_AD.resize(numDof);
    for(int i=0 ; i<numDof ; ++i){
      y_AD[i].diff(i, numDof);
      y_AD[i].val() = y[i];
    }
    // Create vectors of empty AD types for the dependent variables
    vector<Sacado::Fad::DFad<double> > dilatation_AD(numEntries);
    vector<Sacado::Fad::DFad<double> > lambdaNP1_AD(numEntries);
    int numBonds = tempDataManager.getData(deviatoricPlasticExtensionFieldId, Field_ENUM::STEP_N)->MyLength();
    vector<Sacado::Fad::DFad<double> > edpNP1(numBonds);
    vector<Sacado::Fad::DFad<double> > force_AD(numDof);

    // Evaluate the constitutive model using the AD types
    MATERIAL_EVALUATION::computeDilatationAD(x,&y_AD[0],weightedVolume,cellVolume,bondDamage,&dilatation_AD[0],&tempNeighborhoodList[0],tempNumOwnedPoints);
    MATERIAL_EVALUATION::computeInternalForceIsotropicHardeningPlasticAD(x,
                                                                         &y_AD[0],
                                                                         weightedVolume,
                                                                         cellVolume,
                                                                         &dilatation_AD[0],
                                                                         bondDamage,
                                                                         ownedShearCorrectionFactor,
                                                                         edpN,
                                                                         &edpNP1[0],
                                                                         lambdaN,
                                                                         &lambdaNP1_AD[0],
                                                                         &force_AD[0],
                                                                         &tempNeighborhoodList[0],
                                                                         tempNumOwnedPoints,
                                                                         m_bulkModulus,
                                                                         m_shearModulus,
                                                                         m_horizon,
                                                                         m_yieldStress,
                                                                         m_hardeningModulus);

    // Load derivative values into scratch matrix
    // Multiply by volume along the way to convert force density to force
    for(int row=0 ; row<numDof ; ++row){
      for(int col=0 ; col<numDof ; ++col){
        scratchMatrix(row, col) = force_AD[row].dx(col) * cellVolume[row/3];
      }
    }

    // Sum the values into the global tangent matrix (this is expensive).
    jacobian.addValues((int)globalIndices.size(), &globalIndices[0], scratchMatrix.Data());
  }
}

