//==========================================================================
//  AIDA Detector description implementation 
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
#include <DDCond/Type1/Manager_Type1.h>

#include <DD4hep/Detector.h>
#include <DD4hep/World.h>
#include <DD4hep/Errors.h>
#include <DD4hep/Plugins.h>
#include <DD4hep/Printout.h>
#include <DD4hep/Factories.h>
#include <DD4hep/InstanceCount.h>
#include <DD4hep/PluginCreators.h>
#include <DD4hep/ConditionsListener.h>
#include <DD4hep/detail/Handle.inl>
#include <DD4hep/detail/DetectorInterna.h>
#include <DD4hep/detail/ConditionsInterna.h>

#include <DDCond/ConditionsPool.h>
#include <DDCond/ConditionsEntry.h>
#include <DDCond/ConditionsCleanup.h>
#include <DDCond/ConditionsManager.h>
#include <DDCond/ConditionsIOVPool.h>
#include <DDCond/ConditionsDataLoader.h>

using namespace dd4hep::cond;

typedef UpdatePool::UpdateEntries Updates;
typedef dd4hep::RangeConditions RC;

DD4HEP_INSTANTIATE_HANDLE_NAMED(Manager_Type1);

static void* ddcond_create_manager_instance(dd4hep::Detector& description, int, char**)  {
  return (ConditionsManagerObject*)new Manager_Type1(description);
}
DECLARE_DD4HEP_CONSTRUCTOR(DD4hep_ConditionsManager_Type1,ddcond_create_manager_instance)

#define NO_AGE 0

namespace {
  struct Range {};
  struct Discrete {};

  int s_debug = dd4hep::INFO;

  /// Helper: IOV Check function declaration
  template <typename T> const dd4hep::IOVType* check_iov_type(const Manager_Type1* o, const dd4hep::IOV* iov);

  /// Helper: Specialized IOV check
  template <> const dd4hep::IOVType* check_iov_type<void>(const Manager_Type1* o, const dd4hep::IOV* iov)   {
    if ( iov )  {
      const dd4hep::IOVType* typ = iov->iovType ? iov->iovType : o->iovType(iov->type);
      if ( typ )  {
        if ( iov->type == typ->type )  {
          if ( typ->type < o->m_rawPool.size() )  {
            if ( o->m_rawPool[typ->type] != 0 )  {
              return typ;
            }
          }
        }
      }
    }
    return 0;
  }

  /// Helper: Specialized IOV check for discrete IOV values
  template <> const dd4hep::IOVType* check_iov_type<Discrete>(const Manager_Type1* o, const dd4hep::IOV* iov)   {
    const dd4hep::IOVType* typ = check_iov_type<void>(o,iov);
    if ( typ && !iov->has_range() ) return typ;
    return 0;
  }
#if 0
  /// Helper: Specialized IOV check for range IOV values
  template <> const dd4hep::IOVType* check_iov_type<Range>(const Manager_Type1* o, const dd4hep::IOV* iov)   {
    const dd4hep::IOVType* typ = check_iov_type<void>(o,iov);
    if ( typ && iov->has_range() ) return typ;
    return 0;
  }
#endif
  /// Helper: Check conditions result for consistency
  template <typename T> void __check_values__(const Manager_Type1* o, dd4hep::Condition::key_type key, const dd4hep::IOV* iov)  
  {
    if ( !iov )  {
      dd4hep::except("ConditionsMgr","+++ Invalid IOV to access condition: %16llX. [Null-reference]",key);
    }
    const dd4hep::IOVType* typ = check_iov_type<T>(o,iov);
    if ( !typ )  {
      // Severe: We have an unknown IOV type. This is not allowed, 
      // because we do not known hot to handle it.....
      dd4hep::except("ConditionsMgr","+++ Invalid IOV type [%d] to access condition: %16llX.",
                     iov->type, key);
    }
  }

  /// Helper: Check if the conditions range covers the entire IOV span
  bool is_range_complete(const dd4hep::IOV& iov, const RC& conditions)  {
    if ( !conditions.empty() )  {
      // We need to check if the entire range is covered.
      // For every key.second we must find a key.first, which is at least as big
      dd4hep::IOV::Key test=iov.keyData;
      // The range may be returned unordered. Hence, 
      // we have to try to match at most conditions.size() times until we really know
      for( std::size_t j = 0; j < conditions.size(); ++j )  {
        for(const auto& cond : conditions )   {
          const dd4hep::IOV::Key& k = cond->iov->key();
          if ( k.first   <= test.first+1 && k.second >= test.first  ) test.first = k.second;
          if ( k.first+1 <= test.second  && k.second >= test.second ) test.second = k.first;
          //printout(INFO,"Test","IOV: %ld,%ld --> %ld,%ld",k.first,k.second, test.first, test.second);
          if ( test.first >= test.second ) return true;
        }
        if ( test.first <= iov.keyData.first && test.second >= iov.keyData.second ) return false;
      }
    }
    return false;
  }

  template <typename PMF>
  void __callListeners(const Manager_Type1::Listeners& listeners, PMF pmf, dd4hep::Condition& cond)  {
    for(const auto& listener : listeners )
      (listener.first->*pmf)(cond, listener.second);
  }
}

/// Standard constructor
Manager_Type1::Manager_Type1(Detector& description_instance)
  : ConditionsManagerObject(description_instance), ObjectExtensions(typeid(Manager_Type1)),
    m_updateLock(), m_poolLock(), m_updatePool(), m_rawPool(), m_locked(0)
{
  InstanceCount::increment(this);
  declareProperty("MaxIOVTypes",         m_maxIOVTypes=32);
  declareProperty("PoolType",            m_poolType   = "");
  declareProperty("UpdatePoolType",      m_updateType = "DD4hep_ConditionsLinearUpdatePool");
  declareProperty("UserPoolType",        m_userType   = "DD4hep_ConditionsMapUserPool");
  declareProperty("LoaderType",          m_loaderType = "DD4hep_Conditions_multi_Loader");
  m_iovTypes.resize(m_maxIOVTypes,IOVType());
  m_rawPool.resize(m_maxIOVTypes,0);
}

/// Default destructor
Manager_Type1::~Manager_Type1()   {
  for_each(m_rawPool.begin(), m_rawPool.end(), detail::DestroyObject<ConditionsIOVPool*>());
  InstanceCount::decrement(this);
}

void Manager_Type1::initialize()  {
  if ( !m_updatePool.get() )  {
    std::string typ = m_loaderType;
    const void* argv_loader[] = {"ConditionsDataLoader", this, 0};
    const void* argv_pool[] = {this, 0, 0};
    m_loader.reset(createPlugin<ConditionsDataLoader>(typ,m_detDesc,2,argv_loader));
    m_updatePool.reset(createPlugin<UpdatePool>(m_updateType,m_detDesc,2,argv_pool));
    if ( !m_updatePool.get() )  {
      except("ConditionsMgr","+++ The update pool of type %s cannot be created. [%s]",
             m_updateType.c_str(),Errors::noSys().c_str());
    }
    Ref_t ref(m_updatePool.get());
    ref->SetName("updates");
    ref->SetTitle("updates");
  }
}

/// Register new IOV type if it does not (yet) exist.
std::pair<bool, const dd4hep::IOVType*>
Manager_Type1::registerIOVType(std::size_t iov_index, const std::string& iov_name)   {
  if ( iov_index<m_iovTypes.size() )  {
    IOVType& typ = m_iovTypes[iov_index];
    bool eq_type = typ.type == iov_index;
    bool eq_name = typ.name == iov_name;
    if ( eq_type && eq_name )  {
      return { false, &typ };
    }
    else if ( typ.type != 0 && eq_type && !eq_name )  {
      except("ConditionsMgr","Cannot register IOV %s. Type %d already in use!",
             iov_name.c_str(), iov_index);
    }
    typ.name = iov_name;
    typ.type = iov_index;
    m_rawPool[typ.type] = new ConditionsIOVPool(&typ);
    return { true, &typ };
  }
  except("ConditionsMgr","Cannot register IOV section %d of type %d. Value out of bounds: [%d,%d]",
         iov_name.c_str(), iov_index, 0, int(m_iovTypes.size()));
  return { false, nullptr };
}

/// Access IOV by its type
const dd4hep::IOVType* Manager_Type1::iovType (size_t iov_index) const  {
  if ( iov_index<m_iovTypes.size() )  {
    const IOVType& typ = m_iovTypes[iov_index];
    if ( typ.type == iov_index ) return &typ;
  }
  except("ConditionsMgr","Request to access an unregistered IOV type: %d.", iov_index);
  return 0;
}

/// Access IOV by its name
const dd4hep::IOVType* Manager_Type1::iovType (const std::string& iov_name) const   {
  for( const auto& i : m_iovTypes ) 
    if ( i.name == iov_name ) return &i;
  except("ConditionsMgr","Request to access an unregistered IOV type: %s.", iov_name.c_str());
  return 0;
}

/// Register IOV with type and key
ConditionsPool* Manager_Type1::registerIOV(const IOVType& typ, IOV::Key key)   {
  // IOV read and checked. Now register it, but always locked!
  ConditionsIOVPool* pool = m_rawPool[typ.type];
  dd4hep_lock_t      lock(m_poolLock);
  if ( !pool )  {
    m_rawPool[typ.type] = pool = new ConditionsIOVPool(&typ);
  }
  ConditionsIOVPool::Elements::const_iterator i = pool->elements.find(key);
  if ( i != pool->elements.end() )   {
    return (*i).second.get();
  }
  IOV* iov = new IOV(&typ);
  iov->type      = typ.type;
  iov->keyData   = key;
  const void* argv_pool[] = {this, iov, 0};
  std::shared_ptr<ConditionsPool> cond_pool(createPlugin<ConditionsPool>(m_poolType,m_detDesc,2,argv_pool));
  pool->elements.emplace(key,cond_pool);
  printout(INFO,"ConditionsMgr","Created IOV Pool for:%s",iov->str().c_str());
  return cond_pool.get();
}

/// Access conditions multi IOV pool by iov type
ConditionsIOVPool* Manager_Type1::iovPool(const IOVType& iov_type)  const    {
  return m_rawPool[iov_type.type];
}

/// Register new condition with the conditions store. Unlocked version, not multi-threaded
bool Manager_Type1::registerUnlocked(ConditionsPool& pool, Condition cond)   {
  if ( cond.isValid() )  {
    cond->iov  = pool.iov;
    cond->setFlag(Condition::ACTIVE);
    pool.insert(cond);
#if !defined(DD4HEP_MINIMAL_CONDITIONS) && defined(DD4HEP_CONDITIONS_HAVE_NAME)
    printout(DEBUG,"ConditionsMgr","Register condition %016lX %s [%s] IOV:%s",
             cond.key(), cond.name(), cond->address.c_str(), pool.iov->str().c_str());
#elif defined(DD4HEP_CONDITIONS_HAVE_NAME)
    printout(DEBUG,"ConditionsMgr","Register condition %016lX %s IOV:%s",
             cond.key(), cond.name(), pool.iov->str().c_str());
#else
    printout(DEBUG,"ConditionsMgr","Register condition %016lX IOV:%s",
             cond.key(), pool.iov->str().c_str());
#endif
    if ( !m_onRegister.empty() )   {
      __callListeners(m_onRegister, &ConditionsListener::onRegisterCondition, cond);
    }
    return true;
  }
  else if ( !cond.isValid() )
    except("ConditionsMgr","+++ Invalid condition objects may not be registered. [%s]",
           Errors::invalidArg().c_str());
  return false;
}

/// Register a whole block of conditions with identical IOV.
std::size_t Manager_Type1::blockRegister(ConditionsPool& pool, const std::vector<Condition>& cond) const {
  std::size_t result = 0;
  for(auto c : cond)   {
    if ( c.isValid() )    {
      c->iov = pool.iov;
      c->setFlag(Condition::ACTIVE);
      pool.insert(c);
      if ( !m_onRegister.empty() )   {
        __callListeners(m_onRegister, &ConditionsListener::onRegisterCondition, c);
      }
      ++result;
      continue;
    }
    except("ConditionsMgr",
           "+++ Invalid condition objects may not be registered. [%s]",
           Errors::invalidArg().c_str());    
  }
  return result;
}

/// Set a single conditions value to be managed.
/// Requires external lock on update pool!
dd4hep::Condition Manager_Type1::__queue_update(cond::Entry* e)   {
  if ( e )  {
    ConditionsPool*  p = this->ConditionsManagerObject::registerIOV(e->validity);
    Condition condition(e->name,e->type);
    Condition::Object* c = condition.ptr();
    c->value = e->value;
#if !defined(DD4HEP_MINIMAL_CONDITIONS)
    c->comment = "----";
    c->address = "----";
    c->validity = e->validity;
#endif
    c->iov  = p->iov;
    c->hash = ConditionKey::KeyMaker(e->detector,e->name).hash;
    p->insert(c);
    if ( s_debug > INFO )  {
#if defined(DD4HEP_MINIMAL_CONDITIONS)
      ConditionKey::KeyMaker key(c->hash);
      printout(INFO,"Conditions","+++ Loaded condition: %s %08X.%08X to %s",
               e->detector.path().c_str(), key.values.det_key, key.values.item_key,
               c->value.c_str());
#else
      printout(INFO,"Conditions","+++ Loaded condition: %s.%s to %s [%s] V: %s",
               e->detector.path().c_str(), c->name.c_str(),
               c->value.c_str(), c->type.c_str(), c->validity.c_str());
#endif
    }
    return c;
  }
  return Condition();
}

/// Helper to check iov and user pool and create user pool if not present
void Manager_Type1::__get_checked_pool(const IOV& req_iov,
                                       std::unique_ptr<UserPool>& up)
{
  const IOVType* typ = check_iov_type<Discrete>(this, &req_iov);
  if ( typ )  {
    ConditionsIOVPool* pool = m_rawPool[typ->type];
    if ( 0 == up.get() )  {
      const void* argv[] = {this, pool, 0};
      UserPool* p = createPlugin<UserPool>(m_userType,m_detDesc,2,argv);
      up.reset(p);
    }
    return;
  }
  // Invalid IOV type. Throw exception
  except("ConditionsMgr","+++ Unknown IOV type requested to enable conditions. [%s]",
         Errors::invalidArg().c_str());
}

/// Adopt cleanup handler. If a handler is registered, it is invoked at every "prepare" step
void Manager_Type1::adoptCleanup(ConditionsCleanup* cleaner)     {
  m_cleaner.reset(cleaner);
}

/// Clean conditions, which are above the age limit.
int Manager_Type1::clean(const IOVType* typ, int max_age)   {
  int count = 0;
  dd4hep_lock_t lock(m_updateLock);
  ConditionsIOVPool* pool = m_rawPool[typ->type];
  if ( pool )  {
    count += pool->clean(max_age);
  }
  return count;
}

/// Invoke cache cleanup with user defined policy
std::pair<int,int> Manager_Type1::clean(const ConditionsCleanup& cleaner)   {
  std::pair<int,int> count(0,0);
  for( TypedConditionPool::iterator i=m_rawPool.begin(); i != m_rawPool.end(); ++i)  {
    ConditionsIOVPool* p = *i;
    if ( p && cleaner(*p) )  {
      ++count.first;
      count.second += p->clean(cleaner);
    }
  }
  return count;
}

/// Full cleanup of all managed conditions.
std::pair<int,int> Manager_Type1::clear()   {
  std::pair<int,int> count(0,0);
  for( TypedConditionPool::iterator i=m_rawPool.begin(); i != m_rawPool.end(); ++i)  {
    ConditionsIOVPool* p = *i;
    if ( p )  {
      ++count.first;
      count.second += p->clean(0);
    }
  }
  return count;
}

/// Push all pending updates to the conditions store
void Manager_Type1::pushUpdates()   {
  Updates entries;  {
    dd4hep_lock_t lock(m_updateLock);
    m_updatePool->popEntries(entries);
  }
  // Lock global pool so that no other updates happen in the meanwhile
  // which could kill the pool's containers
  dd4hep_lock_t lock(m_poolLock);
  for(const auto& iov_iter : entries )  {
    const UpdatePool::ConditionEntries& ents = iov_iter.second;
    if ( !ents.empty() )  {
      for(Condition c : ents )  {
        c->setFlag(Condition::ACTIVE);
	except("ConditionsMgr",
	       "+++ We should never end up here [%s]. FIXME!!!!",
	       c.str(0).c_str());
        //c->pool->insert(c);
      }
    }
  }
}

/// Retrieve  a condition set given a Detector Element and the conditions name according to their validity
bool Manager_Type1::select(Condition::key_type key,
                           const IOV& req_validity,
                           RangeConditions& conditions)   {
  {
    ConditionsIOVPool* p = 0;
    dd4hep_lock_t locked_action(m_poolLock);
    p = m_rawPool[req_validity.type]; // Existence already checked by caller!
    p->select(key, req_validity, conditions);
  }
  {
    dd4hep_lock_t locked_action(m_updateLock);
    m_updatePool->select_range(key, req_validity, conditions);
  }
  return !conditions.empty();
}

/// Retrieve  a condition set given a Detector Element and the conditions name according to their validity
bool Manager_Type1::select_range(Condition::key_type key,
                                 const IOV& req_validity,
                                 RangeConditions& conditions)
{
  {
    ConditionsIOVPool* p = 0;
    dd4hep_lock_t locked_action(m_poolLock);
    p = m_rawPool[req_validity.type]; // Existence alread checked by caller!
    p->selectRange(key, req_validity, conditions);
  }
  {
    dd4hep_lock_t locked_action(m_updateLock);
    m_updatePool->select_range(key, req_validity, conditions);
  }
  return is_range_complete(req_validity,conditions);
}
#if 0
/// Retrieve a condition given a Detector Element and the conditions name
Condition
Manager_Type1::get(Condition::key_type key, const IOV& iov)
{
  RC conditions;
  __check_values__<Discrete>(this, key, &iov);
  bool rc = select(key, iov, conditions);
  if ( !rc )  {
    dd4hep_lock_t locked_load(m_updateLock);
    m_loader->load_single(key, iov, conditions);
  }
  if ( conditions.size() == 1 )   {
    conditions[0]->flags |= Condition::ACTIVE;
    return conditions[0];
  }
  else if ( conditions.empty() )   {
    except("ConditionsMgr","+++ Condition %16llX for the requested IOV %s do not exist.",
           key, iov.str().c_str());
  }
  else if ( conditions.size() > 1 )  {
    RC::const_iterator start = conditions.begin();
    Condition first = *start;
    printout(ERROR,"ConditionsMgr","+++ Condition %s [%16llX] is ambiguous for IOV %s:",
             first.name(), key, iov.str().c_str());
    for(RC::const_iterator i=start; i!=conditions.end(); ++i)  {
      Condition c = *i;
      printout(ERROR,"ConditionsMgr","+++ %s [%s] = %s",
               c.name(), c->iov->str().c_str(), c->value.c_str());
    }
    except("ConditionsMgr","+++ Condition %s [%16llX] is ambiguous for IOV %s:",
           first.name(), key, iov.str().c_str());
  }
  return Condition();
}

/// Retrieve a condition given a Detector Element and the conditions name
RangeConditions
Manager_Type1::getRange(Condition::key_type key, const IOV& iov)
{
  RC conditions;
  __check_values__<Range>(this, key, &iov);
  bool rc = select_range(key, iov, conditions);
  if ( rc )  {
    return conditions;
  }
  else  {
    dd4hep_lock_t locked_load(m_updateLock);
    m_loader->load_range(key, iov, conditions);
    if ( conditions.empty() )  {
      except("ConditionsMgr","+++ Conditions %16llX for IOV %s do not exist.",
             key, iov.str().c_str());
    }
    conditions.clear();
  }
  rc = select_range(key, iov, conditions);
  if ( !rc )  {
    except("ConditionsMgr","+++ Conditions %16llX for IOV %s do not exist.",
           key, iov.str().c_str());
  }
  return conditions;
}
#endif

/// Prepare all updates for the given keys to the clients with the defined IOV
ConditionsManager::Result
Manager_Type1::prepare(const IOV& req_iov, ConditionsSlice& slice, ConditionUpdateUserContext* ctx)
{
  __get_checked_pool(req_iov, slice.pool);
  /// First push any pending updates and register them to pending pools...
  pushUpdates();
  /// Now update/fill the user pool
  Result res = slice.pool->prepare(req_iov, slice, ctx);
  /// Invoke auto cleanup if registered
  if ( m_cleaner.get() )   {
    this->clean(*m_cleaner);
  }
  return res;
}

/// Load all updates to the clients with the defined IOV (1rst step of prepare)
ConditionsManager::Result
Manager_Type1::load(const IOV& req_iov, ConditionsSlice& slice, ConditionUpdateUserContext* ctx)    {
  __get_checked_pool(req_iov, slice.pool);
  /// First push any pending updates and register them to pending pools...
  pushUpdates();
  /// Now update/fill the user pool
  Result res = slice.pool->load(req_iov, slice, ctx);
  return res;
}

/// Compute all derived conditions with the defined IOV (2nd step of prepare)
ConditionsManager::Result
Manager_Type1::compute(const IOV& req_iov, ConditionsSlice& slice, ConditionUpdateUserContext* ctx)    {
  Result res = slice.pool->compute(req_iov, slice, ctx);
  /// Invoke auto cleanup if registered
  if ( m_cleaner.get() )   {
    this->clean(*m_cleaner);
  }
  return res;
}

/// Create empty user pool object
std::unique_ptr<UserPool> Manager_Type1::createUserPool(const IOVType* iovT)  const  {
  if ( iovT )  {
    ConditionsIOVPool* p = m_rawPool[iovT->type];
    const void* argv[] = {this, p, 0};
    std::unique_ptr<UserPool> pool(createPlugin<UserPool>(m_userType,m_detDesc,2,argv));
    return pool;
  }
  // Invalid IOV type. Throw exception
  except("ConditionsMgr","+++ Unknown IOV type requested to enable conditions. [%s]",
         Errors::invalidArg().c_str());
  return std::unique_ptr<UserPool>();
}

