#ifndef kernel_h
#define kernel_h
#include "types.h"

namespace exafmm {
  int P;                                                        //!< Order of expansions
  real_t D;                                                     //!< Buffer size
  real_t dX[2];                                                 //!< Distance vector
  real_t theta;                                                 //!< Multipole acceptance criterion
  real_t R0;
  real_t X0[2];
#pragma omp threadprivate(dX)                                   //!< Make global variables private

  //!< Weight of smoothing function
  inline real_t weight(Body * B, Cell * C) {
    real_t x = fmin(C->R - std::abs(B->X[0] - C->X[0]), D);
    real_t y = fmin(C->R - std::abs(B->X[1] - C->X[1]), D);
    if (R0 - std::abs(B->X[0] - X0[0]) < D) x = D;
    if (R0 - std::abs(B->X[1] - X0[1]) < D) y = D;
    assert(x >= -D);
    assert(y >= -D);
    x /= D;
    y /= D;
    real_t w = (2 + 3 * x - x * x * x) / 4;
    w *= (2 + 3 * y - y * y * y) / 4;
    return w;
  }

  //!< P2P kernel between cells Ci and Cj
  void P2PX(Cell * Ci, Cell * Cj) {
    Body * Bi = Ci->BODY;                                       // Target body pointer
    Body * Bj = Cj->BODY;                                       // Source body pointer
    for (int i=0; i<Ci->NBODY; i++) {                           // Loop over target bodies
      real_t p = 0, F[2] = {0, 0};                              //  Initialize potential, force
      for (int j=0; j<Cj->NBODY; j++) {                         //  Loop over source bodies
        for (int d=0; d<2; d++) dX[d] = Bi[i].X[d] - Bj[j].X[d];//   Calculate distance vector
        real_t R2 = norm(dX);                                   //   Calculate distance squared
        if (R2 != 0) {                                          //   If not the same point
          real_t invR = 1 / sqrt(R2);                           //    1 / R
          real_t logR = Bj[j].q * log(invR);                    //    q * log(R)
          p += logR;                                            //    Potential
          for (int d=0; d<2; d++) F[d] += dX[d] * Bj[j].q / R2; //    Force
        }                                                       //   End if for same point
      }                                                         //  End loop over source points
      Bi[i].p += p;                                             //  Accumulate potential
      for (int d=0; d<2; d++) Bi[i].F[d] -= F[d];               //  Accumulate force
    }                                                           // End loop over target bodies
  }

    //!< P2P kernel between cells Ci and Cj
  void P2P(Cell * Ci, Cell * Cj) {
    Body * Bi = Ci->BODY;                                       // Target body pointer
    Body * Bj = Cj->BODY;                                       // Source body pointer
    for (int i=0; i<Ci->NBODY; i++) {                           // Loop over target bodies
      real_t p = 0, F[2] = {0, 0};                              //  Initialize potential, force
      real_t wi = weight(&Bi[i], Ci);
      for (int j=0; j<Cj->NBODY; j++) {                         //  Loop over source bodies
        real_t wj = weight(&Bj[j], Cj);
        for (int d=0; d<2; d++) dX[d] = Bi[i].X[d] - Bj[j].X[d];//   Calculate distance vector
        real_t R2 = norm(dX);                                   //   Calculate distance squared
        if (R2 != 0) {                                          //   If not the same point
          real_t invR = 1 / sqrt(R2);                           //    1 / R
          real_t logR = Bj[j].q * log(invR);                    //    q * log(R)
          p += logR * wj;                                       //    Potential
          for (int d=0; d<2; d++) F[d] += dX[d] * Bj[j].q / R2 * wj; //    Force
        }                                                       //   End if for same point
      }                                                         //  End loop over source points
      Bi[i].p += p * wi;                                        //  Accumulate potential
      for (int d=0; d<2; d++) Bi[i].F[d] -= F[d] * wi;          //  Accumulate force
    }                                                           // End loop over target bodies
  }

  //!< P2M kernel for cell C
  void P2M(Cell * C) {
    for (Body * B=C->BODY; B!=C->BODY+C->NBODY; B++) {          // Loop over bodies
      for (int d=0; d<2; d++) dX[d] = B->X[d] - C->X[d];        //  Get distance vector
      real_t w = weight(B, C);
      complex_t Z(dX[0],dX[1]), powZ(1.0, 0.0);                 //  Convert to complex plane
      C->M[0] += B->q * w;                                      //  Add constant term
      for (int n=1; n<P; n++) {                                 //  Loop over coefficients
        powZ *= Z / real_t(n);                                  //   Store z^n / n!
        C->M[n] += powZ * B->q * w;                             //   Add to coefficient
      }                                                         //  End loop
    }                                                           // End loop
  }

  //!< M2M kernel for one parent cell Ci
  void M2M(Cell * Ci) {
    for (Cell * Cj=Ci->CHILD; Cj!=Ci->CHILD+Ci->NCHILD; Cj++) { // Loop over child cells
      for (int d=0; d<2; d++) dX[d] = Cj->X[d] - Ci->X[d];      //  Get distance vector
      for (int k=0; k<P; k++) {                                 //  Loop over coefficients
        complex_t Z(dX[0],dX[1]), powZ(1.0, 0.0);               //   z^0 = 1
        Ci->M[k] += Cj->M[k];                                   //   Add constant term
        for (int n=1; n<=k; n++) {                              //   Loop over k-l
          powZ *= Z / real_t(n);                                //    Store z^(k-l) / (k-l)!
          Ci->M[k] += Cj->M[k-n] * powZ;                        //    Add to coefficient
        }                                                       //   End loop
      }                                                         //  End loop
    }                                                           // End loop
  }

  //!< M2L kernel between cells Ci and Cj
  void M2L(Cell * Ci, Cell * Cj) {
    for (int d=0; d<2; d++) dX[d] = Ci->X[d] - Cj->X[d];        // Get distance vector
    complex_t Z(dX[0],dX[1]), powZn(1.0, 0.0), powZnk(1.0, 0.0), invZ(powZn/Z);// Convert to complex plane
    Ci->L[0] += -Cj->M[0] * log(Z);                             // Log term (for 0th order)
    Ci->L[0] += Cj->M[1] * invZ;                                // Constant term
    powZn = invZ;                                               // 1/z term
    for (int k=2; k<P; k++) {                                   // Loop over coefficients
      powZn *= real_t(k-1) * invZ;                              //  Store (k-1)! / z^k
      Ci->L[0] += Cj->M[k] * powZn;                             //  Add to coefficient
    }                                                           // End loop
    Ci->L[1] += -Cj->M[0] * invZ;                               // Constant term (for 1st order)
    powZn = invZ;                                               // 1/z term
    for (int k=1; k<P; k++) {                                   // Loop over coefficients
      powZn *= real_t(k) * invZ;                                //  Store (k)! / z^k
      Ci->L[1] += -Cj->M[k] * powZn;                            //  Add to coefficient
    }                                                           // End loop
    real_t Cnk = -1;                                            // Fix sign term
    for (int n=2; n<P; n++) {                                   // Loop over
      Cnk *= -1;                                                //  Flip sign
      powZnk *= invZ;                                           //  Store 1 / z^n
      powZn = Cnk * powZnk;                                     //  Combine terms
      for (int k=0; k<P; k++) {                                 //  Loop over
        powZn *= real_t(n+k-1) * invZ;                          //   (n+k-1)! / z^k
        Ci->L[n] += Cj->M[k] * powZn;                           //   Add to coefficient
      }                                                         //  End loop
      powZnk *= real_t(n-1);                                    //  Store (n-1)! / z^n
    }                                                           // End loop
  }

  //!< L2L kernel for one parent cell Cj
  void L2L(Cell * Cj) {
    for (Cell * Ci=Cj->CHILD; Ci<Cj->CHILD+Cj->NCHILD; Ci++) {  // Loop over child cells
      for (int d=0; d<2; d++) dX[d] = Ci->X[d] - Cj->X[d];      //  Get distance vector
      complex_t Z(dX[0],dX[1]);                                 //  Convert to complex plane
      for (int l=0; l<P; l++) {                                 //  Loop over coefficients
        complex_t powZ(1.0, 0.0);                               //   z^0 = 1
        Ci->L[l] += Cj->L[l];                                   //   Add constant term
        for (int k=1; k<P-l; k++) {                             //   Loop over coefficients
          powZ *= Z / real_t(k);                                //    Store z^k / k!
          Ci->L[l] += Cj->L[l+k] * powZ;                        //    Add to coefficient
        }                                                       //   End loop
      }                                                         //  End loop
    }                                                           // End loop
  }

  //!< L2P kernel for cell C
  void L2P(Cell * C) {
    for (Body * B=C->BODY; B!=C->BODY+C->NBODY; B++) {          // Loop over bodies
      real_t w = weight(B, C);
      for (int d=0; d<2; d++) dX[d] = B->X[d] - C->X[d];        //  Get distance vector
      complex_t Z(dX[0],dX[1]), powZ(1.0, 0.0);                 //  Convert to complex plane
      B->p += std::real(C->L[0]) * w;                           //  Add constant term
      B->F[0] += std::real(C->L[1]) * w;                        //  Add constant term
      B->F[1] -= std::imag(C->L[1]) * w;                        //  Add constant term
      for (int n=1; n<P; n++) {                                 //  Loop over coefficients
        powZ *= Z / real_t(n);                                  //   Store z^n / n!
        B->p += std::real(C->L[n] * powZ) * w;                  //   Add real part to solution
        if (n < P-1) {                                          //   Condition for force accumulation
          B->F[0] += std::real(C->L[n+1] * powZ) * w;           //    Add real part to solution
          B->F[1] -= std::imag(C->L[n+1] * powZ) * w;           //    Add real part to solution
        }                                                       //   End condition for force accumulation
      }                                                         //  End loop
    }                                                           // End loop
  }
}

#endif
