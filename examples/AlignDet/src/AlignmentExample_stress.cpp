//==========================================================================
//  AIDA Detector description implementation for LCD
//--------------------------------------------------------------------------
// Copyright (C) Organisation europeenne pour la Recherche nucleaire (CERN)
// All rights reserved.
//
// For the licensing terms see $DD4hepINSTALL/LICENSE.
// For the list of contributors see $DD4hepINSTALL/doc/CREDITS.
//
// Author     : M.Frank
//
//==========================================================================
/* 
   Plugin invocation:
   ==================
   This plugin behaves like a main program.
   Invoke the plugin with something like this:

   geoPluginRun -volmgr -destroy -plugin DD4hep_AlignmentExample_stress \
   -input file:${DD4hep_DIR}/examples/AlignDet/compact/Telescope.xml

   Populate the conditions store by hand for a set of IOVs.
   Then compute the corresponding alignment entries....

*/
// Framework include files
#include "AlignmentExampleObjects.h"
#include "DD4hep/Factories.h"
#include "TStatistic.h"
#include "TTimeStamp.h"
#include "TRandom3.h"

using namespace std;
using namespace DD4hep;
using namespace DD4hep::AlignmentExamples;

/// Plugin function: Alignment program example
/**
 *  Factory: DD4hep_AlignmentExample_stress
 *
 *  \author  M.Frank
 *  \version 1.0
 *  \date    01/12/2016
 */
static int alignment_example (Geometry::LCDD& lcdd, int argc, char** argv)  {

  string input;
  int    num_iov = 10, num_runs = 10;
  bool   arg_error = false;
  for(int i=0; i<argc && argv[i]; ++i)  {
    if ( 0 == ::strncmp("-input",argv[i],4) )
      input = argv[++i];
    else if ( 0 == ::strncmp("-iovs",argv[i],4) )
      num_iov = ::atol(argv[++i]);
    else if ( 0 == ::strncmp("-runs",argv[i],4) )
      num_runs = ::atol(argv[++i]);
    else
      arg_error = true;
  }
  if ( arg_error || input.empty() )   {
    /// Help printout describing the basic command line interface
    cout <<
      "Usage: -plugin <name> -arg [-arg]                                             \n"
      "     name:   factory name     DD4hep_AlignmentExample_stress                  \n"
      "     -input   <string>        Geometry file                                   \n"
      "     -iovs    <number>        Number of parallel IOV slots for processing.    \n"
      "     -runs    <number>        Number of collision loads to be performed.      \n"
      "\tArguments given: " << arguments(argc,argv) << endl << flush;
    ::exit(EINVAL);
  }

  // First we load the geometry
  lcdd.fromXML(input);
  installManagers(lcdd);

  /******************** Initialize the conditions manager *****************/
  ConditionsManager condMgr = ConditionsManager::from(lcdd);
  condMgr["PoolType"]       = "DD4hep_ConditionsLinearPool";
  condMgr["UserPoolType"]   = "DD4hep_ConditionsMapUserPool";
  condMgr["UpdatePoolType"] = "DD4hep_ConditionsLinearUpdatePool";
  condMgr.initialize();
  
  const IOVType*  iov_typ  = condMgr.registerIOVType(0,"run").second;
  if ( 0 == iov_typ )  {
    except("ConditionsPrepare","++ Unknown IOV type supplied.");
  }
  TStatistic cr_stat("Creation"), comp_stat("Computation"), access_stat("Access");

  /******************** Populate the conditions store *********************/
  // Have num_iov possible run-slices [11,20] .... [n*10+1,(n+1)*10]
  for(int i=0; i<num_iov; ++i)  {
    TTimeStamp start;
    IOV iov(iov_typ, IOV::Key(1+i*10,(i+1)*10));
    ConditionsPool* iov_pool = condMgr.registerIOV(*iov.iovType, iov.key());
    AlignmentCreator creator(condMgr, *iov_pool);   // Use a generic creator
    creator.process(lcdd.world(),0,true);           // Create conditions with all deltas
    TTimeStamp stop;
    cr_stat.Fill(stop.AsDouble()-start.AsDouble());
  }

  /******************** Now as usual: create the slice ********************/
  shared_ptr<ConditionsContent> content(new ConditionsContent());
  shared_ptr<ConditionsSlice>   slice(new ConditionsSlice(condMgr,content));
  Conditions::fill_content(condMgr,*content,*iov_typ);
  
  /******************** Register alignments *******************************/
  // Note: We have to load one set of conditions in order to auto-populate
  //       because we need to see if a detector element actually has alignment
  //       conditions. For this we must access the conditions data.
  //       Unfortunate, but unavoidable.
  //
  IOV iov(iov_typ,15);
  condMgr.prepare(iov,*slice);
  slice->pool->flags |= Conditions::UserPool::PRINT_INSERT;

  // Collect all the delta conditions and make proper alignment conditions out of them
  DetElementDeltaCollector delta_collector(slice.get());
  DetElementProcessor<DetElementDeltaCollector> proc(delta_collector);
  proc.process(lcdd.world(),0,true);
  printout(INFO,"Prepare","Got a total of %ld deltas for processing alignments.",
           delta_collector.deltas.size());

  ConditionsManager::Result total_cres;
  AlignmentsCalculator::Result total_ares;
  /******************** Compute  alignments *******************************/
  for(int i=0; i<num_iov; ++i)  {
    TTimeStamp start;
    IOV req_iov(iov_typ,1+i*10);
    shared_ptr<ConditionsSlice> sl(new ConditionsSlice(condMgr,content));
    ConditionsManager::Result cres = condMgr.prepare(req_iov,*sl);
    // Now compute the tranformation matrices
    AlignmentsCalculator calculator;
    AlignmentsCalculator::Result ares = calculator.compute(delta_collector.deltas,*sl);
    TTimeStamp stop;
    total_cres += cres;
    total_ares += ares;
    //sl->manage(alignments.data);
    comp_stat.Fill(stop.AsDouble()-start.AsDouble());
    printout(INFO,"ComputedDerived",
             "Setup %ld conditions (S:%ld,L:%ld,C:%ld,M:%ld) (D:%ld,A:%ld,M:%ld) for IOV:%-12s [%8.3f sec]",
             cres.total(), cres.selected, cres.loaded, cres.computed, cres.missing, 
             delta_collector.deltas.size(),ares.computed, ares.missing, req_iov.str().c_str(), stop.AsDouble()-start.AsDouble());
  }

  // ++++++++++++++++++++++++ Now access the conditions for every IOV....
  TRandom3 random;
  for(int i=0; i<num_runs; ++i)  {
    TTimeStamp start;
    unsigned int rndm = 1+random.Integer(num_iov*10);
    IOV req_iov(iov_typ,rndm);
    // Attach the proper set of conditions to the user pool
    ConditionsManager::Result res = condMgr.prepare(req_iov,*slice);
    TTimeStamp stop;
    total_cres += res;
    access_stat.Fill(stop.AsDouble()-start.AsDouble());
    printout(INFO,"Setup slice: ",
             "Total %ld conditions (S:%6ld,L:%6ld,C:%4ld,M:%ld) for random %4d of type %s. [%8.4f sec]",
             res.total(), res.selected, res.loaded, res.computed, res.missing, rndm,
             iov_typ->str().c_str(), stop.AsDouble()-start.AsDouble());
  }
  printout(INFO,"Statistics","+======= Summary: # of IOV: %3d  # of Runs: %3d ===========================", num_iov, num_runs);
  printout(INFO,"Statistics","+  %-12s:  %11.5g +- %11.4g  RMS = %11.5g  N = %lld",
           cr_stat.GetName(), cr_stat.GetMean(), cr_stat.GetMeanErr(), cr_stat.GetRMS(), cr_stat.GetN());
  printout(INFO,"Statistics","+  %-12s:  %11.5g +- %11.4g  RMS = %11.5g  N = %lld",
           comp_stat.GetName(), comp_stat.GetMean(), comp_stat.GetMeanErr(), comp_stat.GetRMS(), comp_stat.GetN());
  printout(INFO,"Statistics","+  %-12s:  %11.5g +- %11.4g  RMS = %11.5g  N = %lld",
           access_stat.GetName(), access_stat.GetMean(), access_stat.GetMeanErr(), access_stat.GetRMS(), access_stat.GetN());
    printout(INFO,"Statistics",
             "+  Summary: Total %ld conditions used (S:%ld,L:%ld,C:%ld,M:%ld) (A:%ld,M:%ld).",
             total_cres.total(), total_cres.selected, total_cres.loaded,
             total_cres.computed, total_cres.missing, 
             total_ares.computed, total_ares.missing);

  printout(INFO,"Statistics","+==========================================================================");
  // All done.
  return 1;
}

// first argument is the type from the xml file
DECLARE_APPLY(DD4hep_AlignmentExample_stress,alignment_example)
