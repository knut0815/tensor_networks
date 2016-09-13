/// \file hamiltonian.h
/// \brief Construct local Hamiltonian operator terms

#ifndef HAMILTONIAN_H
#define HAMILTONIAN_H

#include <mpo.h>


void ConstructLocalHeisenbergOperators(const int L, const double Jx, const double Jy, const double Jz, const double hext, double **h);

void ConstructLocalBoseHubbardOperators(const int L, const size_t M, const double t, const double U, const double mu, double **h);


void DeleteLocalHamiltonianOperators(const int L, double **h);


//________________________________________________________________________________________________________________________
//


void ConstructBoseHubbardMPO(const int L, const size_t M, const double t, const double U, const double mu, mpo_t *H);



#endif
