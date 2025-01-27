/*
 * ReplicatedSecret.cpp
 *
 */

#ifndef GC_SHARESECRET_HPP
#define GC_SHARESECRET_HPP

#include "ShareSecret.h"

#include "MaliciousRepSecret.h"
#include "Protocols/MaliciousRepMC.h"
#include "ShareThread.h"
#include "Thread.h"
#include "square64.h"

#include "Protocols/Share.h"

#include "Protocols/ReplicatedMC.hpp"
#include "Protocols/Beaver.hpp"
#include "ShareParty.h"
#include "ShareThread.hpp"
#include "Thread.hpp"

namespace GC
{

template<class U>
const int ReplicatedSecret<U>::N_BITS;

template<class U>
const int ReplicatedSecret<U>::default_length;

template<class U>
SwitchableOutput ShareSecret<U>::out;

template<class U>
void ShareSecret<U>::check_length(int n, const Integer& x)
{
	if ((size_t) n < 8 * sizeof(x)
			and (unsigned long long) abs(x.get()) >= (1ULL << n))
		throw out_of_range(
				"public value too long for " + to_string(n) + " bits: "
						+ to_string(x.get()) + "/" + to_string(1ULL << n));
}

template<class U>
void ReplicatedSecret<U>::load_clear(int n, const Integer& x)
{
    this->check_length(n, x);
    *this = x;
}

template<class U>
void ReplicatedSecret<U>::bitcom(Memory<U>& S, const vector<int>& regs)
{
    *this = 0;
    for (unsigned int i = 0; i < regs.size(); i++)
        *this ^= (S[regs[i]] << i);
}

template<class U>
void ReplicatedSecret<U>::bitdec(Memory<U>& S, const vector<int>& regs) const
{
    for (unsigned int i = 0; i < regs.size(); i++)
        S[regs[i]] = (*this >> i) & 1;
}

template<class U>
void ShareSecret<U>::load(vector<ReadAccess<U> >& accesses,
        const Memory<U>& mem)
{
    for (auto access : accesses)
        access.dest = mem[access.address];
}

template<class U>
void ShareSecret<U>::store(Memory<U>& mem,
        vector<WriteAccess<U> >& accesses)
{
    for (auto access : accesses)
        mem[access.address] = access.source;
}

template<class U>
void ShareSecret<U>::store_clear_in_dynamic(Memory<U>& mem,
        const vector<ClearWriteAccess>& accesses)
{
    for (auto access : accesses)
        mem[access.address] = access.value;
}

template<class U>
void ShareSecret<U>::inputb(Processor<U>& processor,
        ProcessorBase& input_processor,
        const vector<int>& args)
{
    auto& party = ShareThread<U>::s();
    typename U::Input input(*party.MC, party.DataF, *party.P);
    input.reset_all(*party.P);

    InputArgList a(args);
    bool interactive = a.n_interactive_inputs_from_me(party.P->my_num()) > 0;
    int dl = U::default_length;

    for (auto x : a)
    {
        if (x.from == party.P->my_num())
        {
            bigint whole_input = processor.get_long_input(x.params,
                    input_processor, interactive);
            for (int i = 0; i < DIV_CEIL(x.n_bits, dl); i++)
                input.add_mine(bigint(whole_input >> (i * dl)).get_si(),
                        min(dl, x.n_bits - i * dl));
        }
        else
            for (int i = 0; i < DIV_CEIL(x.n_bits, dl); i++)
                input.add_other(x.from);
    }

    if (interactive)
        cout << "Thank you" << endl;

    input.exchange();

    for (auto x : a)
    {
        int from = x.from;
        int n_bits = x.n_bits;
        for (int i = 0; i < DIV_CEIL(x.n_bits, dl); i++)
        {
            auto& res = processor.S[x.dest + i];
            int n = min(dl, n_bits - i * dl);
            res = input.finalize(from, n).mask(n);
        }
    }
}

template<class U>
void ShareSecret<U>::reveal_inst(Processor<U>& processor,
        const vector<int>& args)
{
    auto& party = ShareThread<U>::s();
    assert(args.size() % 3 == 0);
    vector<U> shares;
    for (size_t i = 0; i < args.size(); i += 3)
    {
        int n = args[i];
        int r1 = args[i + 2];
        if (n > max(U::default_length, Clear::N_BITS))
            assert(U::default_length == Clear::N_BITS);
        for (int j = 0; j < DIV_CEIL(n, U::default_length); j++)
        {
            shares.push_back(
                    processor.S[r1 + j].mask(
                            min(U::default_length, n - j * U::default_length)));
        }
    }
    assert(party.MC);
    PointerVector<typename U::open_type> opened;
    party.MC->POpen(opened, shares, *party.P);
    for (size_t i = 0; i < args.size(); i += 3)
    {
        int n = args[i];
        int r0 = args[i + 1];
        for (int j = 0; j < DIV_CEIL(n, U::default_length); j++)
        {
            processor.C[r0 + j] = opened.next().mask(
                    min(U::default_length, n - j * U::default_length));
        }
    }
}

template<class U>
BitVec ReplicatedSecret<U>::local_mul(const ReplicatedSecret& other) const
{
    return (*this)[0] * other.sum() + (*this)[1] * other[0];
}

template<class U>
void ShareSecret<U>::and_(
        Processor<U>& processor, const vector<int>& args,
        bool repeat)
{
    ShareThread<U>::s().and_(processor, args, repeat);
}

template<class U>
void ShareSecret<U>::xors(Processor<U>& processor, const vector<int>& args)
{
    ShareThread<U>::s().xors(processor, args);
}

template<class U>
void ReplicatedSecret<U>::trans(Processor<U>& processor,
        int n_outputs, const vector<int>& args)
{
    assert(length == 2);
    for (int k = 0; k < 2; k++)
    {
        for (int j = 0; j < DIV_CEIL(n_outputs, N_BITS); j++)
            for (int l = 0; l < DIV_CEIL(args.size() - n_outputs, N_BITS); l++)
            {
                square64 square;
                size_t input_base = n_outputs + l * N_BITS;
                for (size_t i = input_base; i < min(input_base + N_BITS, args.size()); i++)
                    square.rows[i - input_base] = processor.S[args[i] + j][k].get();
                square.transpose(
                        min(size_t(N_BITS), args.size() - n_outputs - l * N_BITS),
                        min(N_BITS, n_outputs - j * N_BITS));
                int output_base = j * N_BITS;
                for (int i = output_base; i < min(n_outputs, output_base + N_BITS); i++)
                {
                    processor.S[args[i] + l][k] = square.rows[i - output_base];
                }
            }
    }
}

template<class U>
void ReplicatedSecret<U>::reveal(size_t n_bits, Clear& x)
{
    (void) n_bits;
    auto& share = *this;
    vector<BitVec> opened;
    auto& party = ShareThread<U>::s();
    party.MC->POpen(opened, {share}, *party.P);
    x = BitVec::super(opened[0]);
}

template<class U>
void ShareSecret<U>::random_bit()
{
    U res;
    ShareThread<U>::s().DataF.get_one(DATA_BIT, res);
    *this = res;
}

}

#endif
