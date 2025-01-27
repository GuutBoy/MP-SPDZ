
#include "Processor/Program.h"
#include "Processor/Online-Thread.h"
#include "Tools/time-func.h"
#include "Processor/Data_Files.h"
#include "Processor/Machine.h"
#include "Processor/Processor.h"
#include "Networking/CryptoPlayer.h"
#include "Protocols/ShuffleSacrifice.h"
#include "Protocols/LimitedPrep.h"

#include "Processor/Processor.hpp"
#include "Processor/Input.hpp"
#include "Protocols/LimitedPrep.hpp"

#include <iostream>
#include <fstream>
#include <pthread.h>
using namespace std;


template<class sint, class sgf2n>
void thread_info<sint, sgf2n>::Sub_Main_Func()
{
  bigint::init_thread();

  auto tinfo = this;
  Machine<sint, sgf2n>& machine=*(tinfo->machine);
  vector<Program>& progs                = machine.progs;

  int num=tinfo->thread_num;
  BaseMachine::s().thread_num = num;

  auto& queues = machine.queues[num];
  queues->next();

#ifdef DEBUG_THREADS
  fprintf(stderr, "\tI am in thread %d\n",num);
#endif
  Player* player;
  if (machine.use_encryption)
    {
#ifdef VERBOSE
      cerr << "Using encrypted single-threaded communication" << endl;
#endif
      player = new CryptoPlayer(*(tinfo->Nms), num << 16);
    }
  else if (!machine.receive_threads or machine.direct or machine.parallel)
    {
#ifdef VERBOSE
      cerr << "Using single-threaded receiving" << endl;
#endif
      player = new PlainPlayer(*(tinfo->Nms), num << 16);
    }
  else
    {
      cerr << "Using player-specific threads for receiving" << endl;
      player = new ThreadPlayer(*(tinfo->Nms), num << 16);
    }
  Player& P = *player;
#ifdef DEBUG_THREADS
  fprintf(stderr, "\tSet up player in thread %d\n",num);
#endif

  typename sgf2n::MAC_Check* MC2;
  typename sint::MAC_Check*  MCp;

  // Use MAC_Check instead for more than 10000 openings at once
  if (machine.direct)
    {
      cerr << "Using direct communication. If computation stalls, use -m when compiling." << endl;
      MC2 = new typename sgf2n::Direct_MC(*(tinfo->alpha2i),*(tinfo->Nms), num);
      MCp = new typename sint::Direct_MC(*(tinfo->alphapi),*(tinfo->Nms), num);
    }
  else if (machine.parallel)
    {
      cerr << "Using indirect communication with background threads." << endl;
      //MC2 = new Parallel_MAC_Check<gf2n>(*(tinfo->alpha2i),*(tinfo->Nms), num, machine.opening_sum, machine.max_broadcast);
      //MCp = new Parallel_MAC_Check<gfp>(*(tinfo->alphapi),*(tinfo->Nms), num, machine.opening_sum, machine.max_broadcast);
      throw not_implemented();
    }
  else
    {
#ifdef VERBOSE
      cerr << "Using indirect communication." << endl;
#endif
      MC2 = new typename sgf2n::MAC_Check(*(tinfo->alpha2i), machine.opening_sum, machine.max_broadcast);
      MCp = new typename sint::MAC_Check(*(tinfo->alphapi), machine.opening_sum, machine.max_broadcast);
    }

  // Allocate memory for first program before starting the clock
  processor = new Processor<sint, sgf2n>(tinfo->thread_num,P,*MC2,*MCp,machine,progs[0]);
  auto& Proc = *processor;

  bool flag=true;
  int program=-3; 
  // int exec=0;

  typedef typename sint::bit_type::part_type BT;

  // synchronize
#ifdef DEBUG_THREADS
  cerr << "Locking for sync of thread " << num << endl;
#endif
  queues->finished({});

  DataPositions actual_usage(P.num_players());
  Timer thread_timer(CLOCK_THREAD_CPUTIME_ID), wait_timer;
  thread_timer.start();

  while (flag)
    { // Wait until I have a program to run
      wait_timer.start();
      auto job = queues->next();
      program = job.prognum;
      wait_timer.stop();
      //printf("\tRunning program %d\n",program);

      if (program==-1)
        { flag=false;
#ifdef DEBUG_THREADS
          fprintf(stderr, "\tThread %d terminating\n",num);
#endif
        }
      else if (job.type == BITADD_JOB)
        {
          auto& party = GC::ShareThread<typename sint::bit_type>::s();
          if (job.arg < 0)
            {
              SubProcessor<BT> bit_proc(party.MC->get_part_MC(),
                  Proc.Procp.bit_prep, P);
              BitAdder().add(*(vector<vector<BT>>*) job.output,
                  *(vector<vector<vector<BT>>>*) job.input, job.begin, job.end,
                  bit_proc, job.length, -1, job.supply);
            }
          else
            {
              // too late for preprocessing for security reasons
              assert(job.supply);
              LimitedPrep<BT> limited_prep;
              SubProcessor<BT> bit_proc(party.MC->get_part_MC(),
                  limited_prep, P);
              BitAdder().add(*(vector<vector<BT>>*) job.output,
                  *(vector<vector<vector<BT>>>*) job.input, job.begin, job.end,
                  bit_proc, job.length, -1, job.supply);
            }
          queues->finished(job);
        }
      else if (job.type == DABIT_JOB)
        {
          dynamic_cast<RingPrep<sint>&>(Proc.DataF.DataFp).buffer_dabits_without_check(
              *(vector<dabit<sint>>*) job.output, job.begin, job.end,
              Proc.Procp.bit_prep);
          queues->finished(job);
        }
      else if (job.type == MULT_JOB)
        {
          Proc.Procp.protocol.multiply(*(PointerVector<sint>*) job.output,
              *(vector<pair<sint, sint>>*) job.input, job.begin, job.end,
              Proc.Procp);
          queues->finished(job);
        }
      else if (job.type == EDABIT_JOB)
        {
          dynamic_cast<RingPrep<sint>&>(Proc.DataF.DataFp).buffer_edabits_without_check(
              job.length, *(vector<sint>*) job.output,
              *(vector<vector<BT>>*) job.output2, job.begin, job.end);
          queues->finished(job);
        }
      else if (job.type == PERSONAL_JOB)
        {
          auto &party = GC::ShareThread<typename sint::bit_type>::s();
          SubProcessor<BT> bit_proc(party.MC->get_part_MC(),
              Proc.Procp.bit_prep, P);
          dynamic_cast<RingPrep<sint>&>(Proc.DataF.DataFp).buffer_personal_edabits_without_check(
              job.length, *(vector<sint>*) job.output,
              *(vector<vector<BT>>*) job.output2, bit_proc,
              job.arg, job.begin, job.end);
          queues->finished(job);
        }
      else if (job.type == SANITIZE_JOB)
        {
          dynamic_cast<RingPrep<sint>&>(Proc.DataF.DataFp).sanitize(
              *(vector<edabit<sint>>*) job.output, job.length, job.arg,
              job.begin, job.end);
          queues->finished(job);
        }
      else if (job.type == EDABIT_SACRIFICE_JOB)
        {
          ShuffleSacrifice<sint>().edabit_sacrifice_buckets(
              *(vector<edabit<sint>>*) job.output, job.length, job.prognum,
              job.arg, Proc.Procp,
              job.begin, job.end, job.supply);
          queues->finished(job);
        }
      else if (job.type == PERSONAL_TRIPLE_JOB)
        {
          auto &party = GC::ShareThread<typename sint::bit_type>::s();
          SubProcessor<BT> bit_proc(party.MC->get_part_MC(),
              *Proc.Procp.personal_bit_preps.at(job.arg), P);
          Proc.Procp.personal_bit_preps.at(job.arg)->buffer_personal_triples(
              *(vector<array<BT, 3>>*) job.output, job.begin, job.end);
          queues->finished(job);
        }
      else
        { // RUN PROGRAM
          //printf("\tClient %d about to run %d in execution %d\n",num,program,exec);
          Proc.reset(progs[program], job.arg);

          // Bits, Triples, Squares, and Inverses skipping
          Proc.DataF.seekg(job.pos);
          // reset for actual usage
          Proc.DataF.reset_usage();
             
          //printf("\tExecuting program");
          // Execute the program
          progs[program].execute(Proc);

          actual_usage.increase(Proc.DataF.get_usage());

         if (progs[program].usage_unknown())
           { // communicate file positions to main thread
             job.pos.increase(Proc.DataF.get_usage());
           }

          //double elapsed = timeval_diff(&startv, &endv);
          //printf("Thread time = %f seconds\n",elapsed/1000000);
          //printf("\texec = %d\n",exec); exec++;
          //printf("\tMC2.number = %d\n",MC2.number());
          //printf("\tMCp.number = %d\n",MCp.number());

          // MACCheck
          MC2->Check(P);
          MCp->Check(P);
          //printf("\tMAC checked\n");
          P.Check_Broadcast();
          //printf("\tBroadcast checked\n");

         // printf("\tSignalling I have finished\n");
          wait_timer.start();
          queues->finished(job);
	 wait_timer.stop();
       }  
    }

  // protocol check before last MAC check
  Proc.Procp.protocol.check();
  Proc.Proc2.protocol.check();

  // MACCheck
  MC2->Check(P);
  MCp->Check(P);

  //cout << num << " : Checking broadcast" << endl;
  P.Check_Broadcast();
  //cout << num << " : Broadcast checked "<< endl;

  wait_timer.start();
  queues->next();
  wait_timer.stop();

#ifdef VERBOSE
  cerr << num << " : MAC Checking" << endl;
  cerr << "\tMC2.number=" << MC2->number() << endl;
  cerr << "\tMCp.number=" << MCp->number() << endl;

  cerr << "Thread " << num << " timer: " << thread_timer.elapsed() << endl;
  cerr << "Thread " << num << " wait timer: " << wait_timer.elapsed() << endl;
#endif

  // wind down thread by thread
  size_t prep_sent = Proc.DataF.data_sent();
  prep_sent += Proc.share_thread.DataF.data_sent();
  prep_sent += Proc.Procp.bit_prep.data_sent();
  for (auto& x : Proc.Procp.personal_bit_preps)
    prep_sent += x->data_sent();
  delete processor;

  machine.data_sent += P.sent + prep_sent;
  queues->finished(actual_usage);

  delete MC2;
  delete MCp;
  delete player;

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
  OPENSSL_thread_stop();
#endif
}


template<class sint, class sgf2n>
void* thread_info<sint, sgf2n>::Main_Func(void* ptr)
{
#ifndef INSECURE
  try
#endif
  {
      ((thread_info<sint, sgf2n>*)ptr)->Sub_Main_Func();
  }
#ifndef INSECURE
  catch (...)
  {
      thread_info<sint, sgf2n>* ti = (thread_info<sint, sgf2n>*)ptr;
      ti->purge_preprocessing(*ti->machine);
      throw;
  }
#endif
  return 0;
}


template<class sint, class sgf2n>
void thread_info<sint, sgf2n>::purge_preprocessing(Machine<sint, sgf2n>& machine)
{
  cerr << "Purging preprocessed data because something is wrong" << endl;
  try
  {
      Data_Files<sint, sgf2n> df(machine);
      df.purge();
  }
  catch(...)
  {
      cerr << "Purging failed. This might be because preprocessed data is incomplete." << endl
          << "SECURITY FAILURE; YOU ARE ON YOUR OWN NOW!" << endl;
  }
}
