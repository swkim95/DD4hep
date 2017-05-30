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

// Framework include files
#include "DDCond/ConditionsSlice.h"
#include "DDCond/ConditionsIOVPool.h"
#include "DD4hep/InstanceCount.h"
#include "DD4hep/Printout.h"

using namespace DD4hep::Conditions;

/// Initializing constructor
ConditionsSlice::ConditionsSlice(ConditionsManager m) : manager(m)
{
  InstanceCount::increment(this);  
}

/// Initializing constructor
ConditionsSlice::ConditionsSlice(ConditionsManager m, Content c) : manager(m), content(c)
{
  InstanceCount::increment(this);  
}

/// Copy constructor (Special, partial copy only. Hence no assignment!)
ConditionsSlice::ConditionsSlice(const ConditionsSlice& copy)
  : manager(copy.manager), content(copy.content)
{
  InstanceCount::increment(this);  
}

/// Default destructor. 
ConditionsSlice::~ConditionsSlice()   {
  InstanceCount::decrement(this);  
}

/// Clear the conditions access and the user pool.
void ConditionsSlice::reset()   {
  if ( pool.get() ) pool->clear();
}

/// Local optimization: Insert a set of conditions to the slice AND register them to the conditions manager.
bool ConditionsSlice::manage(ConditionsPool* p, Condition condition, ManageFlag flg)   {
  if ( condition.isValid() )  {
    bool ret = false;
    if ( flg&REGISTER_MANAGER )  {
      if ( !p )   {
        DD4hep::except("ConditionsSlice",
                       "manage_condition: Cannot access conditions pool according to IOV:%s.",
                       pool->validity().str().c_str());
      }
      ret = manager.registerUnlocked(*p,condition);
      if ( !ret )  {
        DD4hep::except("ConditionsSlice",
                       "manage_condition: Failed to register condition %016llx according to IOV:%s.",
                       condition->hash, pool->validity().str().c_str());
      }
    }
    if ( flg&REGISTER_POOL )  {
      ret = pool->insert(condition);
      if ( !ret )  {
        DD4hep::except("ConditionsSlice",
                       "manage_condition: Failed to register condition %016llx to user pool with IOV:%s.",
                       condition->hash, pool->validity().str().c_str());
      }
    }
    return ret;
  }
  DD4hep::except("ConditionsSlice",
                 "manage_condition: Cannot manage invalid condition!");
  return false;
}

/// Insert a set of conditions to the slice AND register them to the conditions manager.
bool ConditionsSlice::manage(Condition condition, ManageFlag flg)    {
  ConditionsPool* p = (flg&REGISTER_MANAGER) ? manager.registerIOV(pool->validity()) : 0;
  return manage(p, condition, flg);
}

/// ConditionsMap overload: Add a condition directly to the slice
bool ConditionsSlice::insert(DetElement detector, unsigned int key, Condition condition)   {
  if ( condition.isValid() )  {
    ConditionsPool* p = manager.registerIOV(pool->validity());
    if ( !p )   {
      DD4hep::except("ConditionsSlice",
                     "manage_condition: Cannot access conditions pool according to IOV:%s.",
                     pool->validity().str().c_str());
    }
    bool ret = manager.registerUnlocked(*p,condition);
    if ( !ret )  {
      DD4hep::except("ConditionsSlice",
                     "manage_condition: Failed to register condition %016llx according to IOV:%s.",
                     condition->hash, pool->validity().str().c_str());
    }
    return pool->insert(detector, key, condition);
  }
  DD4hep::except("ConditionsSlice",
                 "insert_condition: Cannot insert invalid condition to the user pool!");
  return false;
}

/// ConditionsMap overload: Access a condition
Condition ConditionsSlice::get(DetElement detector, unsigned int key)  const   {
  return pool->get(detector, key);
}

/// ConditionsMap overload: Interface to scan data content of the conditions mapping
void ConditionsSlice::scan(Condition::Processor& processor) const   {
  pool->scan(processor);
}


namespace  {
  
  struct SliceOper  : public ConditionsSelect  {
    ConditionsContent& content;
    SliceOper(ConditionsContent& c) : content(c) {}
    void operator()(const ConditionsIOVPool::Elements::value_type& v)    {
      v.second->select_all(*this);
    }
    bool operator()(Condition::Object* c)  const  {
      if ( 0 == (c->flags&Condition::DERIVED) )   {
        content.insertKey(c->hash,c->address);
        return true;
      }
#if 0
      // test load info access
      const ConditionsContent::Conditions& cc=content.conditions();
      auto i = cc.find(c->hash);
      std::string* address = (*i).second->data<std::string>();
      if ( address ) {}
#endif
      return true;
    }
    /// Return number of conditions selected
    virtual size_t size()  const { return content.conditions().size(); }
  };
}

/// Populate the conditions slice from the conditions manager (convenience)
void DD4hep::Conditions::fill_content(ConditionsManager mgr,
                                      ConditionsContent& content,
                                      const IOVType& typ)
{
  Conditions::ConditionsIOVPool* iovPool = mgr.iovPool(typ);
  Conditions::ConditionsIOVPool::Elements& pools = iovPool->elements;
  for_each(begin(pools),end(pools),SliceOper(content));
}

