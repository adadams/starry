/**
Defines the surface map class.

*/

#ifndef _STARRY_MAPS_H_
#define _STARRY_MAPS_H_

#include <iostream>
#include <cmath>
#include <Eigen/Core>
#include "rotation.h"
#include "basis.h"
#include "errors.h"
#include "utils.h"

namespace maps {

    using namespace utils;
    using std::abs;
    using std::string;
    using rotation::Wigner;

    // Constant matrices/vectors
    template <class T>
    class ConstantMatrices {

        public:

            const int lmax;
            Eigen::SparseMatrix<T> A1;
            Eigen::SparseMatrix<T> A;
            VectorT<T> rTA1;
            VectorT<T> rT;
            Matrix<T> U;

            // Constructor: compute the matrices
            ConstantMatrices(int lmax) : lmax(lmax) {
                basis::computeA1(lmax, A1);
                basis::computeA(lmax, A1, A);
                //solver::computerT(lmax, rT);
                //rTA1 = rT * A1;
                basis::computeU(lmax, U);
            }

    };

    // ****************************
    // ----------------------------
    //
    //    The surface map class
    //
    // ----------------------------
    // ****************************
    template <class T>
    class Map {

        public:

            const int lmax;                             /**< The highest degree of the map */
            const int N;                                /**< The number of map coefficients */
            Vector<T> y;                                /**< The map coefficients in the spherical harmonic basis */
            Vector<T> p;                                /**< The map coefficients in the polynomial basis */
            Vector<T> g;                                /**< The map coefficients in the Green's basis */
            UnitVector<T> axis;                         /**< The axis of rotation for the map */
            bool Y00_is_unity;                          /**< Flag: are we fixing the constant map coeff at unity? */
            std::map<string, Vector<double>> gradient;  /**< Dictionary of derivatives */
            Vector<T> dFdy;                             /**< Derivative of the flux w/ respect to the map coeffs */
            ConstantMatrices<T> C;                      /**< Constant matrices used throughout the code */

            Vector<T> tmp_vec;
            Vector<T>* tmp_vec_ptr;

        protected:

            Wigner<T> W;

        public:

            // Constructor
            Map(int lmax=2) : lmax(lmax), N((lmax + 1) * (lmax + 1)),
                              y(Vector<T>::Zero(N)), axis(yhat<T>()),
                              C(lmax), W(lmax, (*this).y, (*this).axis) {

                // Initialize
                dFdy = Vector<T>::Zero(N);

                Y00_is_unity = false;
                update();

            }

            // Housekeeping functions
            void update();
            void reset();

            // I/O functions
            void setCoeff(int l, int m, T coeff);
            T getCoeff(int l, int m);
            void setAxis(const UnitVector<T>& new_axis);
            UnitVector<T> getAxis();
            std::string repr();

            // Rotate the base map
            void rotate(const T& theta);

            // Get the intensity of the map at a point
            T evaluate(const T& theta=0, const T& x0=0, const T& y0=0);

    };


    /* ---------------- */
    /*   HOUSEKEEPING   */
    /* ---------------- */


    // Update the maps after the coefficients changed
    template <class T>
    void Map<T>::update() {

        // Update the polynomial and Green's map coefficients
        p = C.A1 * y;
        g = C.A * y;

        // Update the rotation matrix
        W.update();

    }

    // Reset the map
    template <class T>
    void Map<T>::reset() {
        y.setZero(N);
        if (Y00_is_unity) y(0) = 1;
        axis = yhat<T>();
        update();
    }


    /* ---------------- */
    /*        I/O       */
    /* ---------------- */


    // Set the (l, m) coefficient
    template <class T>
    void Map<T>::setCoeff(int l, int m, T coeff) {
        if ((l == 0) && (Y00_is_unity) && (coeff != 1))
            throw errors::ValueError("The Y_{0,0} coefficient is fixed at unity. "
                                     "You probably want to change the body's "
                                     "luminosity instead.");
        if ((0 <= l) && (l <= lmax) && (-l <= m) && (m <= l)) {
            int n = l * l + l + m;
            y(n) = coeff;
            update();
        } else {
            throw errors::IndexError("Invalid value for `l` and/or `m`.");
        }
    }

    // Get the (l, m) coefficient
    template <class T>
    T Map<T>::getCoeff(int l, int m) {
        if ((0 <= l) && (l <= lmax) && (-l <= m) && (m <= l))
            return y(l * l + l + m);
        else throw errors::IndexError("Invalid value for `l` and/or `m`.");
    }

    // TODO: Method to set multiple coeffs at once with less `update` overhead.

    // Set the axis
    template <class T>
    void Map<T>::setAxis(const UnitVector<T>& new_axis) {

        // Normalize it
        axis = new_axis / sqrt(new_axis(0) * new_axis(0) +
                               new_axis(1) * new_axis(1) +
                               new_axis(2) * new_axis(2));

        // Update the rotation matrix
        W.update();

    }

    // Return a copy of the axis
    template <class T>
    UnitVector<T> Map<T>::getAxis() {
        UnitVector<T> axis_out = axis;
        return axis_out;
    }

    // Return a human-readable map string
    template <class T>
    std::string Map<T>::repr() {
        int n = 0;
        int nterms = 0;
        char buf[30];
        std::ostringstream os;
        os << "<STARRY Map: ";
        for (int l = 0; l < lmax + 1; l++) {
            for (int m = -l; m < l + 1; m++) {
                if (abs(y(n)) > 10 * mach_eps<T>()){
                    // Separator
                    if ((nterms > 0) && (y(n) > 0)) {
                        os << " + ";
                    } else if ((nterms > 0) && (y(n) < 0)){
                        os << " - ";
                    } else if ((nterms == 0) && (y(n) < 0)){
                        os << "-";
                    }
                    // Term
                    if ((y(n) == 1) || (y(n) == -1)) {
                        sprintf(buf, "Y_{%d,%d}", l, m);
                        os << buf;
                    } else if (fmod(abs(y(n)), 1.0) < 10 * mach_eps<T>()) {
                        sprintf(buf, "%d Y_{%d,%d}", (int)abs(y(n)), l, m);
                        os << buf;
                    } else if (fmod(abs(y(n)), 1.0) >= 0.01) {
                        sprintf(buf, "%.2f Y_{%d,%d}", abs(y(n)), l, m);
                        os << buf;
                    } else {
                        sprintf(buf, "%.2e Y_{%d,%d}", abs(y(n)), l, m);
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

    // Rotate the base map in-place
    template <class T>
    void Map<T>::rotate(const T& theta) {
        W.rotate(cos(theta), sin(theta), y);
        update();
    }


    /* ------------- */
    /*   INTENSITY   */
    /* ------------- */


    // Evaluate our map at a given (x0, y0) coordinate
    template <class T>
    T Map<T>::evaluate(const T& theta, const T& x0, const T& y0) {

        // Rotate the map into view
        if (theta == 0) {
            tmp_vec_ptr = &p;
        } else {
            W.rotate(cos(theta), sin(theta), tmp_vec);
            tmp_vec = C.A1 * tmp_vec;
            tmp_vec_ptr = &tmp_vec;
        }

        // Check if outside the sphere
        if (x0 * x0 + y0 * y0 > 1.0) return NAN * x0;

        int l, m, mu, nu, n = 0;
        T z0 = sqrt(1.0 - x0 * x0 - y0 * y0);

        // Evaluate each harmonic
        T res = 0;
        for (l=0; l<lmax+1; l++) {
            for (m=-l; m<l+1; m++) {
                if (abs((*tmp_vec_ptr)(n)) > 10 * mach_eps<T>()) {
                    mu = l - m;
                    nu = l + m;
                    if ((nu % 2) == 0) {
                        if ((mu > 0) && (nu > 0))
                            res += (*tmp_vec_ptr)(n) * pow(x0, mu / 2) * pow(y0, nu / 2);
                        else if (mu > 0)
                            res += (*tmp_vec_ptr)(n) * pow(x0, mu / 2);
                        else if (nu > 0)
                            res += (*tmp_vec_ptr)(n) * pow(y0, nu / 2);
                        else
                            res += (*tmp_vec_ptr)(n);
                    } else {
                        if ((mu > 1) && (nu > 1))
                            res += (*tmp_vec_ptr)(n) * pow(x0, (mu - 1) / 2) * pow(y0, (nu - 1) / 2) * z0;
                        else if (mu > 1)
                            res += (*tmp_vec_ptr)(n) * pow(x0, (mu - 1) / 2) * z0;
                        else if (nu > 1)
                            res += (*tmp_vec_ptr)(n) * pow(y0, (nu - 1) / 2) * z0;
                        else
                            res += (*tmp_vec_ptr)(n) * z0;
                    }
                }
                n++;
            }

        }
        return res;

    }

}; // namespace maps

#endif
