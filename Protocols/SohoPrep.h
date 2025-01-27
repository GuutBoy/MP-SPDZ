/*
 * SohoPrep.h
 *
 */

#ifndef PROTOCOLS_SOHOPREP_H_
#define PROTOCOLS_SOHOPREP_H_

template<class T>
class SohoPrep : public SemiHonestRingPrep<T>
{
    typedef typename T::clear::FD FD;

    static PartSetup<FD>* setup;
    static Lock lock;

public:
    static void basic_setup(Player& P);
    static void teardown();

    SohoPrep(SubProcessor<T>* proc, DataPositions& usage) :
            BufferPrep<T>(usage),
            RingPrep<T>(proc, usage), SemiHonestRingPrep<T>(proc, usage)
    {
    }

    void buffer_triples();
    void buffer_squares();
    void buffer_inverses();
    void buffer_bits();
};

#endif /* PROTOCOLS_SOHOPREP_H_ */
