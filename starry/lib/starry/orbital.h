/**
Orbital star/planet/moon system class.

*/

#ifndef _STARRY_ORBITAL_H_
#define _STARRY_ORBITAL_H_

#include <iostream>
#include <cmath>
#include <Eigen/Core>
#include "constants.h"
#include "maps.h"

// Shorthand
template <typename T>
using Vector = Eigen::Matrix<T, Eigen::Dynamic, 1>;
template <typename T>
using UnitVector = Eigen::Matrix<T, 3, 1>;
using maps::Map;
using maps::yhat;
using std::vector;

namespace orbital {

    template <class T> class Body;
    template <class T> class System;
    template <class T> class Star;
    template <class T> class Planet;

    // System class
    template <class T>
    class System {

        public:

            vector<Body<T>*> bodies;
            Vector<T> flux;
            double eps;
            int maxiter;

            // Constructor
            System(vector<Body<T>*> bodies, const double& eps=1.0e-7, const int& maxiter=100) :
                bodies(bodies), eps(eps), maxiter(maxiter) {

                // Propagate settings down
                for (int i = 1; i < bodies.size(); i++) {
                    bodies[i]->eps = eps;
                    bodies[i]->maxiter = maxiter;
                }

                // Set the flag for the primary
                bodies[0]->is_primary = true;

                // Compute the semi-major axes of each planet/satellite
                for (int i = 1; i < bodies.size(); i++) {
                    bodies[i]->is_primary = false;
                    bodies[i]->a = pow((bodies[i]->porb * bodies[i]->porb) *
                                       (BIGG * bodies[0]->m) /
                                       (4 * M_PI * M_PI), (1. / 3.));
                }

            }

            // Methods
            void compute(const Vector<T>& time);

    };

    // Compute the light curve
    template <class T>
    void System<T>::compute(const Vector<T>& time) {

        int i, j, t;
        double xo, yo, ro;
        int p, o;
        int NT = time.size();

        // Allocate arrays
        for (i = 0; i < bodies.size(); i++) {
            bodies[i]->x.resize(NT);
            bodies[i]->y.resize(NT);
            bodies[i]->z.resize(NT);
            bodies[i]->flux.resize(NT);
        }

        // Loop through the timeseries
        for (t = 0; t < NT; t++){

            // Take an orbital step
            for (i = 0; i < bodies.size(); i++) {
                bodies[i]->step(time(t), t);
                bodies[i]->computed = false;
            }

            // Find occultations and compute the flux
            for (i = 0; i < bodies.size(); i++) {
                for (j = i + 1; j < bodies.size(); j++) {
                    // Determine the relative positions of the two bodies
                    if (bodies[j]->z(t) > bodies[i]->z(t)) {
                        o = j;
                        p = i;
                    } else {
                        o = i;
                        p = j;
                    }
                    xo = (bodies[o]->x(t) - bodies[p]->x(t)) / bodies[p]->r;
                    yo = (bodies[o]->y(t) - bodies[p]->y(t)) / bodies[p]->r;
                    ro = bodies[o]->r / bodies[p]->r;
                    // Compute the flux in occultation
                    if (sqrt(xo * xo + yo * yo) < 1 + ro) {
                        bodies[p]->getflux(time(t), t, xo, yo, ro);
                        bodies[p]->computed = true;
                    }
                }
            }

            // Compute the total flux for the remaining bodies
            for (i = 0; i < bodies.size(); i++) {
                if (!bodies[i]->computed) {
                    bodies[i]->getflux(time(t), t);
                    bodies[i]->computed = true;
                }
            }

        }

        // Add up all the fluxes
        flux = Vector<T>::Zero(NT);
        for (i = 0; i < bodies.size(); i++) {
            flux += bodies[i]->flux;
        }

    }

    // Body class
    template <class T>
    class Body {

        protected:

            // Orbital solution variables
            T M;
            T E;
            T f;
            T rorb;
            T cwf;
            T swf;

            // Commonly used variables
            T M0;
            T cosi;
            T sini;
            T cosO;
            T sinO;
            T sqrtonepluse;
            T sqrtoneminuse;
            T ecc2;
            T cosOcosi;
            T sinOcosi;

            // Methods
            void computeM(const T& time);
            void computeE();
            void computef();

        public:

            // Flag
            bool is_primary;
            bool computed;

            // Map stuff
            int lmax;
            UnitVector<T>& u;
            T prot;
            T theta0;
            T r;
            T L;
            Map<T> map;

            // Orbital elements
            T a;
            T m;
            T porb;
            T inc;
            T ecc;
            T w;
            T Omega;
            T lambda0;
            T tref;

            // Settings
            int iErr;
            double eps;
            int maxiter;

            // Orbital position
            Vector<T> x;
            Vector<T> y;
            Vector<T> z;

            // Flux
            Vector<T> flux;

            // Constructor
            Body(// Map stuff
                 int lmax,
                 const T& r,
                 const T& L,
                 UnitVector<T>& u,
                 const T& prot,
                 const T& theta0,
                 // Orbital stuff
                 const T& m,
                 const T& porb,
                 const T& inc,
                 const T& ecc,
                 const T& w,
                 const T& Omega,
                 const T& lambda0,
                 const T& tref) :
                 lmax(lmax),
                 u(u),
                 prot(prot),
                 theta0(theta0),
                 r(r),
                 L(L),
                 map{Map<T>(lmax, L, r)},
                 m(m),
                 porb(porb),
                 inc(inc),
                 ecc(ecc),
                 w(w),
                 Omega(Omega),
                 lambda0(lambda0),
                 tref(tref) { init(); }

            // Initialize orbital variables
            void init() {
                M0 = lambda0 - Omega - w;
                cosi = cos(inc);
                sini = sin(inc);
                cosO = cos(Omega);
                sinO = sin(Omega);
                cosOcosi = cosO * cosi;
                sinOcosi = sinO * cosi;
                sqrtonepluse = sqrt(1 + ecc);
                sqrtoneminuse = sqrt(1 - ecc);
                ecc2 = ecc * ecc;
            };

            // Public methods
            T theta(const T& time);
            void step(const T& time, const int& t);
            void getflux(const T& time, const int& t, const T& xo=0, const T& yo=0, const T& ro=0);

    };

    // Rotation angle as a function of time
    template <class T>
    T Body<T>::theta(const T& time) {
        if ((prot == 0) || isinf(prot))
            return theta0;
        else
            return fmod(theta0 + 2 * M_PI / prot * (time - tref), 2 * M_PI);
    }

    // Compute the visible flux
    template <class T>
    void Body<T>::getflux(const T& time, const int& t, const T& xo, const T& yo, const T& ro){
        flux(t) = map.flux(u, theta(time), xo, yo, ro);
    }

    // Compute the mean anomaly
    template <class T>
    void Body<T>::computeM(const T& time) {
        M = fmod(M0 + 2 * M_PI / porb * (time - tref), 2 * M_PI);
    }

    // Compute the eccentric anomaly. Adapted from
    // https://github.com/lkreidberg/batman/blob/master/c_src/_rsky.c
    template <class T>
    void Body<T>::computeE() {
        // Initial condition
        E = M;

        // The trivial circular case
        if (ecc == 0.) return;

        // Iterate
        for (int iter = 0; iter <= maxiter; iter++) {
            E = E - (E - ecc * sin(E) - M) / (1. - ecc * cos(E));
            if (fabs(E - ecc * sin(E) - M) <= eps) return;
        }

        // Didn't converge: set the flag
        iErr = STARRY_ERR_KEPLER_MAXITER;
        return;
    }

    // Compute the true anomaly
    template <class T>
    void Body<T>::computef() {
        if (ecc == 0.) f = E;
        else f = 2. * atan2(sqrtonepluse * sin(E / 2.),
                            sqrtoneminuse * cos(E / 2.));
    }

    // Compute the instantaneous x, y, and z positions of the
    // body with a simple Keplerian solver.
    template <class T>
    void Body<T>::step(const T& time, const int& t){

        // Primary is fixed at the origin in the Keplerian solver
        if (is_primary) {
            x(t) = 0;
            y(t) = 0;
            z(t) = 0;
            return;
        }

        // Mean anomaly
        computeM(time);

        // Eccentric anomaly
        computeE();

        // True anomaly
        computef();

        // Orbital radius
        rorb = a * (1 - ecc2) / (1. + ecc * cos(f));

        // Murray and Dermott p. 51
        cwf = cos(w + f);
        swf = sin(w + f);
        x(t) = rorb * (cosO * cwf - sinOcosi * swf);
        y(t) = rorb * (sinO * cwf + cosOcosi * swf);
        z(t) = rorb * swf * sini;

        return;

    }

    // Star, Body subclass
    template <class T>
    class Star : public Body<T> {
        public:
            Star(const T& r=RSUN,
                 const T& L=LSUN,
                 const T& m=MSUN) :
                 Body<T>(2, r, L, yhat, INFINITY, 0,
                         m, INFINITY, 0, 0, 0, 0, 0, 0) {}
    };

    // Planet, Body subclass
    template <class T>
    class Planet : public Body<T> {
        public:
            Planet(int lmax=2,
                   const T& r=RJUP,
                   const T& L=1e-5 * LSUN,
                   UnitVector<T>& u=yhat,
                   const T& prot=DAY,
                   const T& theta0=0,
                   const T& porb=DAY,
                   const T& inc=M_PI / 2.,
                   const T& ecc=0,
                   const T& w=0,
                   const T& Omega=0,
                   const T& lambda0=0,
                   const T& tref=0) :
                   Body<T>(lmax, r, L, u, prot, theta0, 0, porb, inc,
                           ecc, w, Omega, lambda0, tref) {}

    };

    // DEBUG! Let's test this thing.
    Vector<double> test(){

        // Sun-like star
        Star<double> star;
        star.map.limbdark(0.40, 0.26);

        // Hot Jupiter, 1 day orbit, tidally locked
        Planet<double> hot_jupiter;
        hot_jupiter.map.set_coeff(0, 0, 3);
        hot_jupiter.map.set_coeff(1, 1, 1);

        // The vector of body pointers
        vector<Body<double>*> bodies;
        bodies.push_back(&star);
        bodies.push_back(&hot_jupiter);

        // The system
        System<double> system(bodies);

        // The time array
        Vector<double> time = Vector<double>::LinSpaced(10000, 0., 5. * DAY);
        system.compute(time);

        // Normalized flux
        return system.flux / system.flux.mean();

    }

}; // namespace orbital

#endif