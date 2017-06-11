#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <gmp.h>

#include "gmp_helper.h"
#include "rsa_digest.h"

/**
 * rsa_key_init() - init gmp elements in key
 *
 * @param   key: pointer to key struct
 * @return
 */
int rsa_key_init(struct rsa_key *key)
{
        if (!key)
                return -EINVAL;

        mpz_inits(key->n, key->p, key->q, key->e, key->d, NULL);

        return 0;
}

/**
 * rsa_key_clean() - free gmp elements in key
 *
 * @param   key: pointer to key struct
 * @return
 */
int rsa_key_clean(struct rsa_key *key)
{
        if (!key)
                return -EINVAL;

        mpz_clears(key->n, key->p, key->q, key->e, key->d, NULL);

        return 0;
}

/**
 * rsa_key_dump() - dump key data to stdout
 *
 * @param   key: pointer to key struct
 * @return
 */
int rsa_key_dump(struct rsa_key *key)
{
        if (!key)
                return -EINVAL;

        gmp_printf("=== KEY FACTORS ===\n");
        gmp_printf("n: %#Zx\np: %#Zx\nq: %#Zx\ne: %#Zx\nd: %#Zx\n",
                   key->n,
                   key->p,
                   key->q,
                   key->e,
                   key->d);
        gmp_printf("=== END ===\n");

        return 0;
}

/**
 * rsa_public_key_init() - init gmp elements in key
 *
 * @param   key: pointer to key struct
 * @return
 */
int rsa_public_key_init(struct rsa_public *key)
{
        if (!key)
                return -EINVAL;

        mpz_inits(key->n, key->d, NULL);

        return 0;
}

/**
 * rsa_public_key_clean() - free gmp elements in key
 *
 * @param   key: pointer to key struct
 * @return
 */
int rsa_public_key_clean(struct rsa_public *key)
{
        if (!key)
                return -EINVAL;

        mpz_clears(key->n, key->d, NULL);

        return 0;
}

/**
 * rsa_private_key_init() - init gmp elements in key
 *
 * @param   key: pointer to key struct
 * @return
 */
int rsa_private_key_init(struct rsa_private *key)
{
        if (!key)
                return -EINVAL;

        mpz_inits(key->n, key->e, NULL);

        return 0;
}

/**
 * rsa_private_key_clean() - free gmp elements in key
 *
 * @param   key: pointer to key struct
 * @return
 */
int rsa_private_key_clean(struct rsa_private *key)
{
        if (!key)
                return -EINVAL;

        mpz_clears(key->n, key->e, NULL);

        return 0;
}

/**
 * primality_test() - Solovay-Strassen primality test
 *
 * @param   n: a value to test
 * @param   k: determines the accuracy of the test
 * @return  1 on *probably* prime, 0 on composite
 */
int primality_test(const mpz_t n, uint64_t k)
{
        mpz_t a;
        mpz_t x;
        mpz_t t;

        mpz_inits(t, a, x, NULL);

        if (!mpz_cmp_ui(n, 1))
                return 0;

        if (!mpz_cmp_ui(n, 2))
                return 1;

        mpz_mod_ui(t, n, 2);
        if (!mpz_cmp_ui(t, 0))
                return 0;

        /* temporary variable */
        mpz_sub_ui(t, n, 2);
        while (k-- > 0) {
                /* choose a randomly in the range [2, n - 1] */
                __mpz_urandomm(a, t);
                mpz_add_ui(a, a, 2);

                mpz_set_ui(x, (uint64_t)mpz_jacobi(a, n));

                /* x == -1 */
                if (!mpz_cmp_si(x, -1)) {
                        mpz_set(x, n);
                        mpz_sub_ui(x, x, 1);
                }

                /*
                 * ( a^((n-1)/2) ) (mod n)
                 */
                if (mpz_cmp_ui(x, 0)) {
                        mpz_set(t, n);
                        mpz_sub_ui(t, t, 1);
                        mpz_div_ui(t, t, 2);

                        mpz_powm(a, a, t, n);

                        if (!mpz_cmp(a, x))
                                return 1;
                }
        }

        mpz_clears(t, a, x, NULL);

        return 0;
}

/**
 * generate_n_p_q() - generate N P Q factors in key
 *
 * @param   n: n to write
 * @param   p: p to write
 * @param   q: q to write
 * @param   len_n: the length of n, aka the length of RSA key
 * @return  0 on success
 */
int generate_n_p_q(mpz_t n, mpz_t p, mpz_t q, uint64_t len_n)
{
        if (!n || !p || !q || !len_n)
                return -EINVAL;

        if (len_n % 2)
                return -EINVAL;

        while (1) {
                mpz_rand_bitlen(p, len_n / 2);
                mpz_rand_bitlen(q, len_n / 2);

                mpz_mul(n, p, q);
                if (mpz_check_binlen(n, len_n))
                        continue;

                while (1) {
                        mpz_rand_bitlen(p, len_n / 2);

                        if (primality_test(p, PRIMALITY_TEST_ACCURACY) ==
                            NUM_COMPOSITE)
                                continue;

                        break;
                }

                while (1) {
                        mpz_rand_bitlen(q, len_n / 2);

                        if (primality_test(q, PRIMALITY_TEST_ACCURACY) ==
                            NUM_COMPOSITE)
                                continue;

                        break;
                }

                mpz_mul(n, p, q);
                if (!mpz_check_binlen(n, len_n))
                        break;
        }

        return 0;
}

/**
 * generate_e_d() - generate E and D factors
 *
 * @param   e: e to write
 * @param   d: d to write
 * @param   p: p factor
 * @param   q: q factor
 * @return  0 on success
 */
int generate_e_d(mpz_t e, mpz_t d, const mpz_t p, const mpz_t q)
{
        mpz_t phi;
        mpz_t p1;
        mpz_t q1;
        mpz_t t;

        if (!e || !d)
                return -EINVAL;

        mpz_inits(phi, p1, q1, t, NULL);

        /* phi = (p - 1) * (q - 1) */
        mpz_sub_ui(p1, p, 1);
        mpz_sub_ui(q1, q, 1);
        mpz_mul(phi, p1, q1);

        /* A very common choice for e is 65537 */
        mpz_set_ui(e, 65537);

        /* XXX: duplicated */
        mpz_gcd(t, e, phi);
        if (mpz_cmp_ui(t, 1)) {
                fprintf(stderr, "gcd() (e, phi) failed!\n");
                return -EFAULT;
        }

        mpz_invert(d, e, phi);

        /* test (e * d) % phi = 1 */
        mpz_mul(t, e, d);
        mpz_mod(t, t, phi);
        if (mpz_cmp_ui(t, 1)) {
                fprintf(stderr, "(e * d) %% phi = 1 failed!\n");
        }

        mpz_clears(phi, p1, q1, t, NULL);

        return 0;
}

/**
 * generate_key() - generate RSA key at given length
 *
 * @param   key: key struct to write
 * @param   len_key: rsa key length
 * @return  0 on success
 */
int generate_key(struct rsa_key *key, uint64_t len_key)
{
        if (!key)
                return -EINVAL;

        if (generate_n_p_q(key->n, key->p, key->q, len_key)) {
                fprintf(stderr, "failed to generate N, P, Q factors\n");
                return -EFAULT;
        }

        if (generate_e_d(key->e, key->d, key->p, key->q)) {
                fprintf(stderr, "failed to generate E, Q factors\n");
                return -EFAULT;
        }

        return 0;
}

/**
 * generate_public_key()
 *
 * @param   key: struct hold all key factors
 * @param   pkey: public key struct
 * @return  0 on success
 */
int generate_public_key(struct rsa_key *key, struct rsa_public *pkey)
{
        if (!key || !key->n || !key->d)
                return -EINVAL;

        mpz_set(pkey->n, key->n);
        mpz_set(pkey->d, key->d);

        return 0;
}

/**
 * generate_private_key()
 *
 * @param   key: struct hold all key factors
 * @param   pkey: private key struct
 * @return  0 on success
 */
int generate_private_key(struct rsa_key *key, struct rsa_private *pkey)
{
        if (!key || !key->n || !key->e)
                return -EINVAL;

        mpz_set(pkey->n, key->n);
        mpz_set(pkey->e, key->e);

        return 0;
}