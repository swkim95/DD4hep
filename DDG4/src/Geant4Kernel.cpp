// $Id: Geant4Kernel.cpp 603 2013-06-13 21:15:14Z markus.frank $
//====================================================================
//  AIDA Detector description implementation for LCD
//--------------------------------------------------------------------
//
//  Author     : M.Frank
//
//====================================================================

// Framework include files
#include "DD4hep/LCDD.h"
#include "DD4hep/Printout.h"
#include "DD4hep/Primitives.h"
#include "DD4hep/InstanceCount.h"

#include "DDG4/Geant4Kernel.h"
#include "DDG4/Geant4ActionPhase.h"

#include "DDG4/Geant4RunAction.h"
#include "DDG4/Geant4PhysicsList.h"
#include "DDG4/Geant4EventAction.h"
#include "DDG4/Geant4SteppingAction.h"
#include "DDG4/Geant4TrackingAction.h"
#include "DDG4/Geant4StackingAction.h"
#include "DDG4/Geant4GeneratorAction.h"
#include "DDG4/Geant4SensDetAction.h"
#include "DDG4/Geant4DetectorConstruction.h"

#include "G4RunManager.hh"

// C/C++ include files
#include <stdexcept>
#include <algorithm>

using namespace std;
using namespace DD4hep::Simulation;


/// Standard constructor
Geant4Kernel::PhaseSelector::PhaseSelector(Geant4Kernel* kernel) : m_kernel(kernel)  {
}

/// Copy constructor
Geant4Kernel::PhaseSelector::PhaseSelector(const PhaseSelector& c) : m_kernel(c.m_kernel)  {
}

/// Phase access to the map
Geant4ActionPhase& Geant4Kernel::PhaseSelector::operator[](const std::string& name)  const   {
  Geant4ActionPhase* phase = m_kernel->getPhase(name);
  if ( phase ) {
    return *phase;
  }
  throw runtime_error(format("Geant4Kernel","Attempt to access the nonexisting phase '%s'",name.c_str()));
}

/// Standard constructor
Geant4Kernel::Geant4Kernel(LCDD& lcdd)
: m_context(0), m_runManager(0), m_generatorAction(0), m_runAction(0), m_eventAction(0), 
  m_trackingAction(0), m_steppingAction(0), m_stackingAction(0), m_sensDetActions(0),
  m_physicsList(0), m_lcdd(lcdd), phase(this)
{
#if 0
  registerSequence(m_runAction,      "RunAction");
  registerSequence(m_eventAction,    "EventAction");
  registerSequence(m_steppingAction, "SteppingAction");
  registerSequence(m_trackingAction, "TrackingAction");
  registerSequence(m_stackingAction, "StackingAction");
  registerSequence(m_generatorAction,"GeneratorAction");
#endif
  m_sensDetActions = new Geant4SensDetSequences();
  m_context = new Geant4Context(this); 
  m_lcdd.addExtension<Geant4Kernel>(this);
  InstanceCount::increment(this);
}

/// Default destructor
Geant4Kernel::~Geant4Kernel()   {
  destroyPhases();
  for_each(m_globalFilters.begin(),m_globalFilters.end(),releaseObjects(m_globalFilters));
  for_each(m_globalActions.begin(),m_globalActions.end(),releaseObjects(m_globalActions));
  deletePtr(m_runManager);
  releasePtr(m_physicsList);
  releasePtr(m_stackingAction);
  releasePtr(m_steppingAction);
  releasePtr(m_trackingAction);
  releasePtr(m_eventAction);
  releasePtr(m_generatorAction);
  releasePtr(m_runAction);
  deletePtr(m_sensDetActions);
  deletePtr(m_context);
  m_lcdd.destroyInstance();
  InstanceCount::decrement(this);
}

/// Instance accessor
Geant4Kernel& Geant4Kernel::instance(LCDD& lcdd)    {
  static Geant4Kernel obj(lcdd);
  return obj;
}

/// Accessof the Geant4Kernel object from the LCDD reference extension (if present and registered)
Geant4Kernel& Geant4Kernel::access(LCDD& lcdd)   {
  Geant4Kernel* kernel = lcdd.extension<Geant4Kernel>();
  if ( !kernel )  {
    throw runtime_error(format("Geant4Kernel","DDG4: The LCDD object has no registered "
			       "extension of type Geant4Kernel [No-Extension]"));
  }
  return *kernel;
}

/// Access to the Geant4 run manager
G4RunManager& Geant4Kernel::runManager()  {
  if ( m_runManager ) return *m_runManager;
  return *(m_runManager = new G4RunManager);
}

/// Construct detector geometry using lcdd plugin
void Geant4Kernel::loadGeometry(const std::string& compact_file)   {
  char* arg = (char*)compact_file.c_str();
  m_lcdd.apply("DD4hepXMLLoader",1,&arg);
  //return *this;
}

// Utility function to load XML files
void Geant4Kernel::loadXML(const char* fname)   {
  const char* args[] = {fname,0};
  m_lcdd.apply("DD4hepXMLLoader",1,(char**)args);
  //return *this;
}

void Geant4Kernel::configure()   {
  Geant4Exec::configure(*this);
  //return *this;
}

void Geant4Kernel::initialize()   {
  Geant4Exec::initialize(*this);
  //return *this;
}

void Geant4Kernel::run()   {
  Geant4Exec::run(*this);
  //return *this;
}

void Geant4Kernel::terminate()   {
  Geant4Exec::terminate(*this);
  destroyPhases();
  for_each(m_globalFilters.begin(),m_globalFilters.end(),releaseObjects(m_globalFilters));
  for_each(m_globalActions.begin(),m_globalActions.end(),releaseObjects(m_globalActions));
  deletePtr(m_runManager);
  releasePtr(m_physicsList);
  releasePtr(m_stackingAction);
  releasePtr(m_steppingAction);
  releasePtr(m_trackingAction);
  releasePtr(m_eventAction);
  releasePtr(m_generatorAction);
  releasePtr(m_runAction);
  deletePtr(m_sensDetActions);
  deletePtr(m_context);
  //return *this;
}


template <class C> bool 
Geant4Kernel::registerSequence(C*& seq,const std::string& name)  {
  if ( !name.empty() )   {
    seq = new C(m_context,name);
    return true;
  }
  throw runtime_error(format("Geant4Kernel","DDG4: The action '%s' not found. [Action-NotFound]",name.c_str()));
}

/// Register action by name to be retrieved when setting up and connecting action objects
/** Note: registered actions MUST be unique.
 *  However, not all actions need to registered....
 *  Only register those, you later need to retrieve by name.
 */
Geant4Kernel& Geant4Kernel::registerGlobalAction(Geant4Action* action)    {
  if ( action )  {
    string nam = action->name();
    GlobalActions::const_iterator i = m_globalActions.find(nam);
    if ( i == m_globalActions.end() )  {
      action->addRef();
      m_globalActions[nam] = action;
      return *this;
    }
    throw runtime_error(format("Geant4Kernel","DDG4: The action '%s' is already globally "
			       "registered. [Action-Already-Registered]",nam.c_str()));
  }
  throw runtime_error(format("Geant4Kernel","DDG4: Attempt to globally register an invalid "
			     "action. [Action-Invalid]"));
}

/// Retrieve action from repository
Geant4Action* Geant4Kernel::globalAction(const std::string& action_name, bool throw_if_not_present)   {
  GlobalActions::iterator i = m_globalActions.find(action_name);
  if ( i == m_globalActions.end() )  {
    if ( throw_if_not_present )  {
      throw runtime_error(format("Geant4Kernel","DDG4: The action '%s' is not already globally "
				 "registered. [Action-Missing]",action_name.c_str()));
    }
    return 0;
  }
  return (*i).second;
}

/// Register filter by name to be retrieved when setting up and connecting filter objects
/** Note: registered filters MUST be unique.
 *  However, not all filters need to registered....
 *  Only register those, you later need to retrieve by name.
 */
Geant4Kernel& Geant4Kernel::registerGlobalFilter(Geant4Action* filter)    {
  if ( filter )  {
    string nam = filter->name();
    GlobalActions::const_iterator i = m_globalFilters.find(nam);
    if ( i == m_globalFilters.end() )  {
      filter->addRef();
      m_globalFilters[nam] = filter;
      return *this;
    }
    throw runtime_error(format("Geant4Kernel","DDG4: The filter '%s' is already globally "
			       "registered. [Filter-Already-Registered]",nam.c_str()));
  }
  throw runtime_error(format("Geant4Kernel","DDG4: Attempt to globally register an invalid "
			     "filter. [Filter-Invalid]"));
}

/// Retrieve filter from repository
Geant4Action* Geant4Kernel::globalFilter(const std::string& filter_name, bool throw_if_not_present)   {
  GlobalActions::iterator i = m_globalFilters.find(filter_name);
  if ( i == m_globalFilters.end() )  {
    if ( throw_if_not_present )  {
      throw runtime_error(format("Geant4Kernel","DDG4: The filter '%s' is not already globally "
				 "registered. [Filter-Missing]",filter_name.c_str()));
    }
    return 0;
  }
  return (*i).second;
}

/// Access phase by name
Geant4ActionPhase* Geant4Kernel::getPhase(const std::string& nam)    {
  Phases::const_iterator i=m_phases.find(nam);
  if ( i != m_phases.end() )  {
    return (*i).second;
  }
  throw runtime_error(format("Geant4Kernel","DDG4: The Geant4 action phase '%s' "
			     "does not exist. [No-Entry]", nam.c_str()));
}

/// Add a new phase 
Geant4ActionPhase* Geant4Kernel::addPhase(const std::string& nam, const type_info& arg0, 
					  const type_info& arg1, const type_info& arg2, bool throw_on_exist)  {
  Phases::const_iterator i = m_phases.find(nam);
  if ( i == m_phases.end() )  {
    Geant4ActionPhase* p = new Geant4ActionPhase(m_context,nam,arg0,arg1,arg2);
    m_phases.insert(make_pair(nam,p));
    return p;
  }
  else if ( throw_on_exist )  {
    throw runtime_error(format("Geant4Kernel","DDG4: The Geant4 action phase %s "
			       "already exists. [Already-Exists]", nam.c_str()));
  }
  return (*i).second;
}

/// Remove an existing phase from the phase. If not existing returns false
bool Geant4Kernel::removePhase(const std::string& nam)  {
  Phases::iterator i=m_phases.find(nam);
  if ( i != m_phases.end() )  {
    delete (*i).second;
    m_phases.erase(i);
    return true;
  }
  return false;
}

/// Destroy all phases. To be called only at shutdown
void Geant4Kernel::destroyPhases()   {
  for_each(m_phases.begin(),m_phases.end(),destroyObjects(m_phases));
}

/// Access generator action sequence
Geant4GeneratorActionSequence* Geant4Kernel::generatorAction(bool create)  {
  if ( !m_generatorAction && create ) registerSequence(m_generatorAction,"GeneratorAction");
  return m_generatorAction;
}

/// Access run action sequence
Geant4RunActionSequence* Geant4Kernel::runAction(bool create)  {
  if ( !m_runAction && create ) registerSequence(m_runAction,"RunAction");
  return m_runAction;
}

/// Access event action sequence
Geant4EventActionSequence* Geant4Kernel::eventAction(bool create)  {
  if ( !m_eventAction && create ) registerSequence(m_eventAction,"EventAction");
  return m_eventAction;
}

/// Access stepping action sequence
Geant4SteppingActionSequence* Geant4Kernel::steppingAction(bool create)  {
  if ( !m_steppingAction && create ) registerSequence(m_steppingAction,"SteppingAction");
  return m_steppingAction;
}

/// Access tracking action sequence
Geant4TrackingActionSequence* Geant4Kernel::trackingAction(bool create)  {
  if ( !m_trackingAction && create ) registerSequence(m_trackingAction,"TrackingAction");
  return m_trackingAction;
}

/// Access stacking action sequence
Geant4StackingActionSequence* Geant4Kernel::stackingAction(bool create)  {
  if ( !m_stackingAction && create ) registerSequence(m_stackingAction,"StackingAction");
  return m_stackingAction;
}

/// Access to the sensitive detector sequences from the kernel object
Geant4SensDetSequences& Geant4Kernel::sensitiveActions() const   {
  return *m_sensDetActions;
}

/// Access to the sensitive detector action from the kernel object
Geant4SensDetActionSequence* Geant4Kernel::sensitiveAction(const string& nam) const   {
  Geant4SensDetActionSequence* ptr = m_sensDetActions->find(nam);
  if ( ptr ) return ptr;
  ptr = new Geant4SensDetActionSequence(context(),nam);
  m_sensDetActions->insert(nam,ptr);
  return ptr;
}

/// Access to the physics list
Geant4PhysicsListActionSequence* Geant4Kernel::physicsList(bool create)   {
  if ( !m_physicsList && create ) registerSequence(m_physicsList,"PhysicsList");
  return m_physicsList;
}

