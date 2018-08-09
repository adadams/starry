/**
Defines the surface map class.

*/

#ifndef _STARRY_MAPS_H_
#define _STARRY_MAPS_H_

#include <iostream>
#include <cmath>
#include <Eigen/Core>
#include <type_traits>
#include <vector>
#include "rotation.h"
#include "basis.h"
#include "errors.h"
#include "utils.h"
#include "solver.h"

namespace maps {

    using namespace utils;
    using std::abs;
    using std::string;
    using std::to_string;
    using rotation::Wigner;
    using basis::Basis;
    using solver::Greens;


    /**
    The main surface map class.

    */
    template <class T>
    class Map {

        public:

            const int lmax;                                                     /**< The highest degree of the map */
            const int N;                                                        /**< The number of map coefficients */
            const int NW;                                                       /**< The number of wavelengths */
            Matrix<T> dI;                                                       /**< Gradient of the intensity */
            std::vector<string> dI_names;                                       /**< Names of each of the params in the intensity gradient */
            Matrix<T> dF;                                                       /**< Gradient of the flux */
            std::vector<string> dF_names;                                       /**< Names of each of the params in the flux gradient */

        private:

            Matrix<T> y;                                                        /**< The map coefficients in the spherical harmonic basis */
            Matrix<T> p;                                                        /**< The map coefficients in the polynomial basis */
            Matrix<T> g;                                                        /**< The map coefficients in the Green's basis */
            UnitVector<T> axis;                                                 /**< The axis of rotation for the map */
            Basis<T> B;                                                         /**< Basis transform stuff */
            Wigner<T> W;                                                        /**< The class controlling rotations */
            Greens<T> G;                                                        /**< The occultation integral solver class */
            Greens<ADScalar<T, 2>> G_grad;                                      /**< The occultation integral solver class w/ AutoDiff capability */
            T tol;                                                              /**< Machine epsilon */

            // Temporary vectors
            Matrix<T> mtmp;                                                     /**< A temporary surface map vector */
            Matrix<T> mtmp2;                                                    /**< A temporary surface map vector */
            Vector<T> vtmp;                                                     /**< A temporary surface map vector */
            VectorT<T> vTtmp;                                                   /**< A temporary surface map vector */
            VectorT<T> pT;                                                      /**< The polynomial basis vector */
            Matrix<T> Ry;                                                       /**< The rotated spherical harmonic vector */
            VectorT<T> pTA1;                                                    /**< Polynomial basis dotted into change of basis matrix */
            Matrix<T> dRdthetay;                                                /**< Derivative of `Ry` with respect to `theta` */
            Matrix<T>* ptr_A1Ry;                                                /**< Pointer to rotated polynomial vector */
            ADScalar<T, 2> x0_grad;                                             /**< x position AD type for map evaluation */
            ADScalar<T, 2> y0_grad;                                             /**< y position AD type for map evaluation */
            VectorT<ADScalar<T, 2>> pT_grad;                                    /**< Polynomial basis AD type */
            ADScalar<T, 2> b_grad;                                              /**< Occultor impact parameter AD type for flux evaluation */
            ADScalar<T, 2> ro_grad;                                             /**< Occultor radius AD type for flux evaluation */
            VectorT<ADScalar<T, 2>> sT_grad;                                    /**< Occultation solution vector AD type */
            Matrix<T>* ptr_Ry;                                                  /**< Pointer to rotated spherical harmonic vector */
            Matrix<T>* ptr_RRy;                                                 /**< Pointer to rotated spherical harmonic vector */
            Matrix<T> ARRy;                                                     /**< The `ARRy` term in `s^TARRy` */
            VectorT<T> sTA;                                                     /**< The solution vector in the sph harm basis */
            VectorT<T> sTAR;                                                    /**< The solution vector in the rotated sph harm basis */
            VectorT<T> sTAdRdtheta;                                             /**< The derivative of `sTAR` with respect to `theta` */
            VectorT<T> dFdb;                                                    /**< Gradient of the flux with respect to the impact parameter */

            // Private methods
            template <typename V>
            inline void poly_basis(const V& x0, const V& y0, VectorT<V>& basis);
            inline Vector<T> evaluate_with_gradient(const T& theta_deg,
                                                    const T& x0_, const T& y0_);
            inline Vector<T> flux_with_gradient(const T& theta_deg,
                                                const T& xo_, const T& yo_,
                                                const T& ro_);

        public:

            /**
            Instantiate a `Map`.

            */
            Map(int lmax=2, int nwav=1) :
                lmax(lmax),
                N((lmax + 1) * (lmax + 1)),
                NW(nwav),
                dI(Matrix<T>::Zero(3 + N, NW)),
                dI_names({"theta", "x", "y"}),
                dF(Matrix<T>::Zero(4 + N, NW)),
                dF_names({"theta", "xo", "yo", "ro"}),
                y(Matrix<T>::Zero(N, NW)),
                axis(yhat<T>()),
                B(lmax),
                W(lmax, NW, (*this).y, (*this).axis),
                G(lmax),
                G_grad(lmax),
                tol(mach_eps<T>()),
                // Temporary stuff
                mtmp(Matrix<T>::Zero(N, NW)),
                mtmp2(Matrix<T>::Zero(N, NW)),
                vtmp(Vector<T>::Zero(N)),
                vTtmp(VectorT<T>::Zero(N)),
                pT(VectorT<T>::Zero(N)),
                Ry(Matrix<T>::Zero(N, NW)),
                pTA1(VectorT<T>::Zero(N)),
                dRdthetay(Matrix<T>::Zero(N, NW)),
                x0_grad(0, Vector<T>::Unit(2, 0)),
                y0_grad(0, Vector<T>::Unit(2, 1)),
                pT_grad(VectorT<ADScalar<T, 2>>::Zero(N)),
                b_grad(0, Vector<T>::Unit(2, 0)),
                ro_grad(0, Vector<T>::Unit(2, 1)),
                sT_grad(VectorT<ADScalar<T, 2>>::Zero(N)),
                ARRy(Matrix<T>::Zero(N, NW)),
                sTA(VectorT<T>::Zero(N)),
                sTAR(VectorT<T>::Zero(N)),
                sTAdRdtheta(VectorT<T>::Zero(N)),
                dFdb(VectorT<T>::Zero(NW))
                {

                // Populate the gradient names
                for (int l = 0; l < lmax + 1; l++) {
                    for (int m = -l; m < l + 1; m++) {
                        dI_names.push_back(string("Y_{" + to_string(l) +
                                           "," + to_string(m) + "}"));
                        dF_names.push_back(string("Y_{" + to_string(l) +
                                           "," + to_string(m) + "}"));
                    }
                }

                // Initialize misc. map properties
                reset();

            }

            // Housekeeping functions
            void update();
            void reset();

            // I/O functions
            void setYlm(int l, int m, const VectorT<T>& coeff);
            void setYlm(int l, int m, T coeff);
            VectorT<T> getYlm(int l, int m) const;
            void setAxis(const UnitVector<T>& axis_);
            UnitVector<T> getAxis() const;
            Matrix<T> getY() const;
            void setY(const Matrix<T>& y_);
            Matrix<T> getP() const;
            Matrix<T> getG() const;
            VectorT<T> getR() const;
            VectorT<T> getS() const;
            std::string __repr__(int iwav=0);

            // Rotate the base map
            void rotate(const T& theta_);

            // Evaluate the intensity at a point
            inline Vector<T> evaluate(const T& theta_=0, const T& x_=0,
                                      const T& y_=0, bool gradient=false);

            // Compute the flux
            inline Vector<T> flux(const T& theta_=0, const T& xo_=0,
                                  const T& yo_=0, const T& ro_=0,
                                  bool gradient=false);

    };

    /* ---------------- */
    /*   HOUSEKEEPING   */
    /* ---------------- */


    /**
    Update the maps after the coefficients changed

    */
    template <class T>
    void Map<T>::update() {

        // Update the polynomial and Green's map coefficients
        p = B.A1 * y;
        g = B.A * y;

        // Update the rotation matrix
        W.update();

    }

    /**
    Reset the map

    */
    template <class T>
    void Map<T>::reset() {
        y.setZero(N, NW);
        axis = yhat<T>();
        update();
    }


    /* ---------------- */
    /*        I/O       */
    /* ---------------- */


    /**
    Set the (l, m) spherical harmonic coefficient

    */
    template <class T>
    void Map<T>::setYlm(int l, int m, const VectorT<T>& coeff) {
        if (coeff.size() != NW)
            throw errors::ValueError("Size mismatch in the wavelength dimension.");
        if ((0 <= l) && (l <= lmax) && (-l <= m) && (m <= l)) {
            int n = l * l + l + m;
            y.row(n) = coeff;
            update();
        } else {
            throw errors::IndexError("Invalid value for `l` and/or `m`.");
        }
    }

    /**
    Set the (l, m) spherical harmonic coefficient (scalar override)

    */
    template <class T>
    void Map<T>::setYlm(int l, int m, T coeff) {
        setCoeff(l, m, Vector<T>::Constant(1, coeff));
    }

    /**
    Get the (l, m) spherical harmonic coefficient

    */
    template <class T>
    VectorT<T> Map<T>::getYlm(int l, int m) const {
        if ((0 <= l) && (l <= lmax) && (-l <= m) && (m <= l)) {
            return y.row(l * l + l + m);
        } else
            throw errors::IndexError("Invalid value for `l` and/or `m`.");
    }

    /**
    Set the axis

    */
    template <class T>
    void Map<T>::setAxis(const UnitVector<T>& axis_) {

        // Set it and normalize it
        axis(0) = axis_(0);
        axis(1) = axis_(1);
        axis(2) = axis_(2);
        axis = axis / sqrt(axis(0) * axis(0) +
                           axis(1) * axis(1) +
                           axis(2) * axis(2));

        // Update the rotation matrix
        W.update();

    }

    /**
    Return a copy of the axis

    */
    template <class T>
    UnitVector<T> Map<T>::getAxis() const {
        return axis;
    }

    /**
    Get the spherical harmonic vector

    */
    template <class T>
    Matrix<T> Map<T>::getY() const {
        return y;
    }

    /**
    Set the spherical harmonic vector

    */
    template <class T>
    void Map<T>::setY(const Matrix<T>& y_) {
        if ((y_.rows() == y.rows()) && (y_.cols() == y.cols())) {
            y = y_;
            update();
        } else {
            throw errors::ValueError("Dimension mismatch in `y`.");
        }
    }

    /**
    Get the polynomial vector

    */
    template <class T>
    Matrix<T> Map<T>::getP() const {
        return p;
    }

    /**
    Get the Green's vector

    */
    template <class T>
    Matrix<T> Map<T>::getG() const {
        return g;
    }

    /**
    Get the rotation solution vector

    */
    template <class T>
    VectorT<T> Map<T>::getR() const {
        return B.rT;
    }

    /**
    Get the occultation solution vector

    */
    template <class T>
    VectorT<T> Map<T>::getS() const {
        return G.sT;
    }

    /**
    Return a human-readable map string at wavelength index `iwav`

    */
    template <class T>
    std::string Map<T>::__repr__(int iwav) {
        int n = 0;
        int nterms = 0;
        char buf[30];
        std::ostringstream os;
        os << "<STARRY Map: ";
        for (int l = 0; l < lmax + 1; l++) {
            for (int m = -l; m < l + 1; m++) {
                if (abs(y(n, iwav)) > 10 * mach_eps<T>()){
                    // Separator
                    if ((nterms > 0) && (y(n, iwav) > 0)) {
                        os << " + ";
                    } else if ((nterms > 0) && (y(n, iwav) < 0)){
                        os << " - ";
                    } else if ((nterms == 0) && (y(n, iwav) < 0)){
                        os << "-";
                    }
                    // Term
                    if ((y(n, iwav) == 1) || (y(n, iwav) == -1)) {
                        sprintf(buf, "Y_{%d,%d}", l, m);
                        os << buf;
                    } else if (fmod(abs(y(n, iwav)), 1.0) < 10 * mach_eps<T>()) {
                        sprintf(buf, "%d Y_{%d,%d}", (int)abs(y(n, iwav)), l, m);
                        os << buf;
                    } else if (fmod(abs(y(n, iwav)), 1.0) >= 0.01) {
                        sprintf(buf, "%.2f Y_{%d,%d}", double(abs(y(n, iwav))), l, m);
                        os << buf;
                    } else {
                        sprintf(buf, "%.2e Y_{%d,%d}", double(abs(y(n, iwav))), l, m);
                        os << buf;
                    }
                    nterms++;
                }
                n++;
            }
        }
        if (nterms == 0)
            os << "Null";
        os << ">";
        return std::string(os.str());
    }


    /* ------------- */
    /*   ROTATIONS   */
    /* ------------- */


    /**
    Rotate the base map in-place given `theta` in **degrees**

    */
    template <class T>
    void Map<T>::rotate(const T& theta_) {
        T theta = theta_ * (pi<T>() / 180.);
        W.rotate(cos(theta), sin(theta), y);
        update();
    }


    /* ------------- */
    /*   INTENSITY   */
    /* ------------- */

    /**
    Compute the polynomial basis at a point; templated for AD capability

    */
    template <class T> template <typename U>
    inline void Map<T>::poly_basis(const U& x0, const U& y0, VectorT<U>& basis) {
        int l, m, mu, nu, n = 0;
        U z0 = sqrt(1.0 - x0 * x0 - y0 * y0);
        for (l=0; l<lmax+1; l++) {
            for (m=-l; m<l+1; m++) {
                mu = l - m;
                nu = l + m;
                if ((nu % 2) == 0) {
                    if ((mu > 0) && (nu > 0))
                        basis(n) = pow(x0, mu / 2) * pow(y0, nu / 2);
                    else if (mu > 0)
                        basis(n) = pow(x0, mu / 2);
                    else if (nu > 0)
                        basis(n) = pow(y0, nu / 2);
                    else
                        basis(n) = 1;
                } else {
                    if ((mu > 1) && (nu > 1))
                        basis(n) = pow(x0, (mu - 1) / 2) *
                                       pow(y0, (nu - 1) / 2) * z0;
                    else if (mu > 1)
                        basis(n) = pow(x0, (mu - 1) / 2) * z0;
                    else if (nu > 1)
                        basis(n) = pow(y0, (nu - 1) / 2) * z0;
                    else
                        basis(n) = z0;
                }
                n++;
            }
        }
    }

    /**
    Evaluate the map at a given (x0, y0) coordinate

    */
    template <class T>
    inline Vector<T> Map<T>::evaluate(const T& theta_, const T& x_,
                                      const T& y_, bool gradient) {

        // If we're computing the gradient as well,
        // call the specialized function
        if (gradient)
            return evaluate_with_gradient(theta_, x_, y_);

        // Convert to internal types
        T x0 = x_;
        T y0 = y_;
        T theta = theta_ * (pi<T>() / 180.);

        // Rotate the map into view
        if (theta == 0) {
            ptr_A1Ry = &p;
        } else {
            W.rotate(cos(theta), sin(theta), Ry);
            mtmp = B.A1 * Ry;
            ptr_A1Ry = &mtmp;
        }

        // Check if outside the sphere
        if (x0 * x0 + y0 * y0 > 1.0)
            return Vector<T>::Constant(NW, NAN);

        // Compute the polynomial basis
        poly_basis(x0, y0, pT);

        // Dot the coefficients in to our polynomial map
        return pT * (*ptr_A1Ry);

    }

    /**
    Evaluate the map at a given (x0, y0) coordinate and compute the gradient

    */
    template <class T>
    inline Vector<T> Map<T>::evaluate_with_gradient(const T& theta_,
                                                    const T& x_,
                                                    const T& y_) {

        // Convert to internal type
        T x0 = x_;
        T y0 = y_;
        T theta = theta_ * (pi<T>() / 180.);

        // Rotate the map into view
        W.compute(cos(theta), sin(theta));
        if (theta == 0) {
            ptr_A1Ry = &p;
        } else {
            for (int l = 0; l < lmax + 1; l++)
                Ry.block(l * l, 0, 2 * l + 1, NW) =
                    W.R[l] * y.block(l * l, 0, 2 * l + 1, NW);
            mtmp = B.A1 * Ry;
            ptr_A1Ry = &mtmp;
        }

        // Check if outside the sphere
        if (x0 * x0 + y0 * y0 > 1.0) {
            dI = Matrix<T>::Constant(N, NW, NAN);
            return Vector<T>::Constant(NW, NAN);
        }

        // Compute the polynomial basis and its x and y derivs
        x0_grad.value() = x0;
        y0_grad.value() = y0;
        poly_basis(x0_grad, y0_grad, pT_grad);
        dI.row(1) = VectorT<T>::Constant(NW, 0);
        dI.row(2) = VectorT<T>::Constant(NW, 0);
        for (int i = 0; i < N; i++) {
            dI.row(1) += pT_grad(i).derivatives()(0) * (*ptr_A1Ry).row(i);
            dI.row(2) += pT_grad(i).derivatives()(1) * (*ptr_A1Ry).row(i);
            pT(i) = pT_grad(i).value();
        }

        // Compute the map derivs
        pTA1 = pT * B.A1;
        if (theta == 0) {
            for (int i = 0; i < N; i++)
                dI.row(3 + i) = VectorT<T>::Constant(NW, pTA1(i));
        } else {
            for (int l = 0; l < lmax + 1; l++)
                vtmp.segment(l * l, 2 * l + 1) =
                    pTA1.segment(l * l, 2 * l + 1) * W.R[l];
            for (int i = 0; i < N; i++)
                dI.row(3 + i) = VectorT<T>::Constant(NW, vtmp(i));
        }

        // Compute the theta deriv
        for (int l = 0; l < lmax + 1; l++)
            dRdthetay.block(l * l, 0, 2 * l + 1, NW) =
                W.dRdtheta[l] * y.block(l * l, 0, 2 * l + 1, NW);
        dI.row(0) = pTA1 * dRdthetay * (pi<T>() / 180.);

        // Dot the coefficients in to our polynomial map
        return pT * (*ptr_A1Ry);

    }


    /* ------------- */
    /*      FLUX     */
    /* ------------- */


    /**
    Compute the flux during or outside of an occultation
    */
    template <class T>
    inline Vector<T> Map<T>::flux(const T& theta_, const T& xo_,
                                  const T& yo_, const T& ro_, bool gradient) {

        // If we're computing the gradient as well,
        // call the specialized function
        if (gradient)
            return flux_with_gradient(theta_, xo_, yo_, ro_);

        // Convert to internal types
        T xo = xo_;
        T yo = yo_;
        T ro = ro_;
        T theta = theta_ * (pi<T>() / 180.);

        // Impact parameter
        T b = sqrt(xo * xo + yo * yo);

        // Check for complete occultation
        if (b <= ro - 1)
            return Vector<T>::Constant(NW, 0);

        // Rotate the map into view
        if (theta == 0) {
            ptr_Ry = &y;
        } else {
            W.rotate(cos(theta), sin(theta), Ry);
            mtmp = Ry;
            ptr_Ry = &mtmp;
        }

        // No occultation
        if ((b >= 1 + ro) || (ro == 0)) {

            // This is very easy!
            return (B.rTA1 * (*ptr_Ry));

        // Occultation
        } else {

            // Align occultor with the +y axis
            if ((b > 0) && ((xo != 0) || (yo < 0))) {
                W.rotatez(yo / b, xo / b, *ptr_Ry, mtmp);
                ptr_Ry = &mtmp;
            }

            // Perform the rotation + change of basis
            ARRy = B.A * (*ptr_Ry);

            // Compute the sT vector (sparsely)
            for (int n = 0; n < N; ++n) {
                for (int i = 0; i < NW; ++i) {
                    G.skip(n) = true;
                    if (abs(ARRy(n, i)) > tol) {
                        G.skip(n) = false;
                        break;
                    }
                }
            }
            G.compute(b, ro);

            // Dot the result in and we're done
            return G.sT * ARRy;

        }

    }

    /**
    Compute the flux during or outside of an occultation and its gradient
    */
    template <class T>
    inline Vector<T> Map<T>::flux_with_gradient(const T& theta_deg,
                                                const T& xo_,
                                                const T& yo_, const T& ro_) {

        // Convert to internal type
        T xo = xo_;
        T yo = yo_;
        T ro = ro_;
        T theta = theta_deg * (pi<T>() / 180.);

        // Impact parameter
        T b = sqrt(xo * xo + yo * yo);

        // Check for complete occultation
        if (b <= ro - 1) {
            dF = Matrix<T>::Constant(N, NW, NAN);
            return Vector<T>::Constant(NW, 0.0);
        }

        // Rotate the map into view
        W.compute(cos(theta), sin(theta));
        if (theta == 0) {
            ptr_Ry = &y;
        } else {
            for (int l = 0; l < lmax + 1; l++)
                Ry.block(l * l, 0, 2 * l + 1, NW) =
                    W.R[l] * y.block(l * l, 0, 2 * l + 1, NW);
            mtmp = Ry;
            ptr_Ry = &mtmp;
        }

        // No occultation
        if ((b >= 1 + ro) || (ro == 0)) {

            // Compute the theta deriv
            for (int l = 0; l < lmax + 1; l++)
                dRdthetay.block(l * l, 0, 2 * l + 1, NW) =
                    W.dRdtheta[l] * y.block(l * l, 0, 2 * l + 1, NW);
            dF.row(0) = B.rTA1 * dRdthetay * (pi<T>() / 180.);

            // The x, y, and r derivs are trivial
            dF.row(1) = Vector<T>::Zero(NW);
            dF.row(2) = Vector<T>::Zero(NW);
            dF.row(3) = Vector<T>::Zero(NW);

            // Compute the map derivs
            pTA1 = pT * B.A1;
            if (theta == 0) {
                for (int i = 0; i < N; i++)
                    dF.row(4 + i) = VectorT<T>::Constant(NW, B.rTA1(i));
            } else {
                for (int l = 0; l < lmax + 1; l++)
                    vtmp.segment(l * l, 2 * l + 1) =
                        B.rTA1.segment(l * l, 2 * l + 1) * W.R[l];
                for (int i = 0; i < N; i++)
                    dF.row(4 + i) = VectorT<T>::Constant(NW, vtmp(i));
            }

            // We're done!
            return (B.rTA1 * (*ptr_Ry));

        // Occultation
        } else {

            // Align occultor with the +y axis
            T xo_b = xo / b;
            T yo_b = yo / b;
            if ((b > 0) && ((xo != 0) || (yo < 0))) {
                W.rotatez(yo_b, xo_b, *ptr_Ry, mtmp2);
                ptr_RRy = &mtmp2;
            } else {
                W.cosmt = Vector<T>::Constant(N, 1.0);
                W.sinmt = Vector<T>::Constant(N, 0.0);
                ptr_RRy = ptr_Ry;
            }

            // Perform the rotation + change of basis
            ARRy = B.A * (*ptr_RRy);

            // Compute the sT vector using AutoDiff
            b_grad.value() = b;
            ro_grad.value() = ro;
            G_grad.compute(b_grad, ro_grad);

            // Compute the b and ro derivs
            dFdb = VectorT<T>::Constant(NW, 0);
            dF.row(3) = VectorT<T>::Constant(NW, 0);
            for (int i = 0; i < N; i++) {

                // b deriv
                dFdb += G_grad.sT(i).derivatives()(0) * ARRy.row(i);

                // ro deriv
                dF.row(3) += G_grad.sT(i).derivatives()(1) * ARRy.row(i);

                // Store the value of s^T
                G.sT(i) = G_grad.sT(i).value();

            }

            // Solution vector in spherical harmonic basis
            sTA = G.sT * B.A;

            // Compute stuff involving the Rprime rotation matrix
            int m;
            for (int l = 0; l < lmax + 1; l++) {
                for (int j = 0; j < 2 * l + 1; j++) {
                    m = j - l;
                    sTAR(l * l + j) = sTA(l * l + j) * W.cosmt(l * l + j) +
                                      sTA(l * l + 2 * l - j) * W.sinmt(l * l + j);
                    sTAdRdtheta(l * l + j) = sTA(l * l + 2 * l - j) * m * W.cosmt(l * l + j) -
                                             sTA(l * l + j) * m * W.sinmt(l * l + j);
                }
            }

            // Compute the xo and yo derivs using the chain rule
            vTtmp = (sTAdRdtheta * (*ptr_Ry)) / b;
            dF.row(1) = xo_b * dFdb + yo_b * vTtmp;
            dF.row(2) = yo_b * dFdb - xo_b * vTtmp;

            // Compute the theta deriv
            for (int l = 0; l < lmax + 1; l++)
                dRdthetay.block(l * l, 0, 2 * l + 1, NW) =
                    W.dRdtheta[l] * y.block(l * l, 0, 2 * l + 1, NW);
            dF.row(0) = sTAR * dRdthetay * (pi<T>() / 180.);

            // Compute the map derivs
            if (theta == 0) {
                for (int i = 0; i < N; i++)
                    dF.row(4 + i) = VectorT<T>::Constant(NW, sTAR(i));
            } else {
                for (int l = 0; l < lmax + 1; l++)
                    vtmp.segment(l * l, 2 * l + 1) =
                        sTAR.segment(l * l, 2 * l + 1) * W.R[l];
                for (int i = 0; i < N; i++)
                    dF.row(4 + i) = VectorT<T>::Constant(NW, vtmp(i));
            }

            // Dot the result in and we're done
            return G.sT * ARRy;

        }

    }

}; // namespace maps

#endif
