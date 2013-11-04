// $Id$
//====================================================================
//  AIDA Detector description implementation
//--------------------------------------------------------------------
//
//  Author     : M.Frank
//
//====================================================================
#ifndef DDG4_FACTORIES_H
#define DDG4_FACTORIES_H

#ifndef __CINT__
#include "Reflex/PluginService.h"
#endif
#include "RVersion.h"

// Framework include files
#include "DDG4/Defs.h"
#include <string>
#include <map>

// Forward declarations
class G4ParticleDefinition;
class G4VSensitiveDetector; 
class G4MagIntegratorStepper;
class G4EquationOfMotion;
class G4MagneticField;
class G4Mag_EqRhs;
class G4VPhysicsConstructor;
class G4VProcess;

namespace DD4hep { 
  namespace Geometry   {
    class DetElement;
    class LCDD; 
  }
  namespace Simulation {
    class Geant4Context;
    class Geant4Action;
    class Geant4Converter;
    class Geant4Sensitive;

    class G4SDFactory; 
    template <typename T> class Geant4SetupAction  {
    public:
      static long create(Geometry::LCDD& lcdd, const Geant4Converter& cnv, const std::map<std::string,std::string>& attrs);
    };
    template <typename T> class Geant4SensitiveDetectorFactory  {
    public:
      static G4VSensitiveDetector* create(const std::string& name, DD4hep::Geometry::LCDD& lcdd);
    };
  }
}

namespace {

  template < typename P > class Factory<P, long(DD4hep::Geometry::LCDD*,const DD4hep::Simulation::Geant4Converter*,const std::map<std::string,std::string>*)> {
  public:
    typedef DD4hep::Geometry::LCDD              LCDD;
    typedef DD4hep::Simulation::Geant4Converter cnv_t;
    typedef std::map<std::string,std::string>   attrs_t;
    static void Func(void *retaddr, void*, const std::vector<void*>& arg, void*) {
      LCDD*    lcdd = (LCDD* )arg[0];
      cnv_t*   cnv  = (cnv_t*)arg[1];
      attrs_t* attr = (attrs_t*)arg[2];
      long ret = DD4hep::Simulation::Geant4SetupAction<P>::create(*lcdd,*cnv,*attr);
      new(retaddr) (long)(ret);
    }
  };

  /// Factory to create Geant4 sensitive detectors
  template <typename P> class Factory<P, G4VSensitiveDetector*(std::string,DD4hep::Geometry::LCDD*)> {
  public:  typedef G4VSensitiveDetector SD;
    static void Func(void *retaddr, void*, const std::vector<void*>& arg, void*) 
    { *(void**)retaddr = DD4hep::Simulation::Geant4SensitiveDetectorFactory<P>::create(*(std::string*)arg[0],*(DD4hep::Geometry::LCDD*)arg[1]); }
    //{  *(SD**)retaddr = (SD*)new P(*(std::string*)arg[0], *(DD4hep::Geometry::LCDD*)arg[1]);           }
  };

  template <typename P> class Factory<P, DD4hep::Simulation::G4SDFactory*()> {
  public:  typedef DD4hep::Simulation::G4SDFactory SD;
    static void Func(void *retaddr, void*, const std::vector<void*>& arg, void*) 
      {  *(SD**)retaddr = (SD*)new P();           }
  };

  /// Factory to create Geant4 sensitive detectors
  template <typename P> class Factory<P, DD4hep::Simulation::Geant4Sensitive*(DD4hep::Simulation::Geant4Context*,std::string,DD4hep::Geometry::DetElement*,DD4hep::Geometry::LCDD*)> {
  public:  typedef DD4hep::Simulation::Geant4Sensitive _S;
    static void Func(void *retaddr, void*, const std::vector<void*>& arg, void*)     {
      *(_S**)retaddr = (_S*)new P((DD4hep::Simulation::Geant4Context*)arg[0],*(std::string*)arg[1],*(DD4hep::Geometry::DetElement*)arg[2],*(DD4hep::Geometry::LCDD*)arg[3]);
    }
  };

  /// Factory to create Geant4 sensitive detectors
  template <typename P> class Factory<P, DD4hep::Simulation::Geant4Action*(DD4hep::Simulation::Geant4Context*,std::string)> {
  public:  typedef DD4hep::Simulation::Geant4Action ACT;
    static void Func(void *retaddr, void*, const std::vector<void*>& arg, void*) 
    { *(ACT**)retaddr = (ACT*)new P((DD4hep::Simulation::Geant4Context*)arg[0], *(std::string*)arg[1]); }
  };

  /// Templated Factory for constructors without argument
  template <typename P,typename R> struct FF0 {
    static void Func(void *ret, void*, const std::vector<void*>&, void*) { *(R*)ret = (R)(new P()); }
  };
  /// Templated Factory for constructors with exactly 1 argument
  template <typename P,typename R,typename A0> struct FF1 {
    static void Func(void *ret, void*, const std::vector<void*>& arg, void*) { *(R*)ret = (R)(new P((A0)arg[0])); }
  };

  /// Factory to create Geant4 steppers
  template <typename P> class Factory<P, G4MagIntegratorStepper*(G4EquationOfMotion*)>  : public FF1<P,G4MagIntegratorStepper*,G4EquationOfMotion*>{};
  /// Factory to create Geant4 steppers
  template <typename P> class Factory<P, G4MagIntegratorStepper*(G4Mag_EqRhs*)> : public FF1<P,G4MagIntegratorStepper*,G4Mag_EqRhs*> {};
  /// Factory to create Geant4 equations of motion for magnetic fields
  template <typename P> class Factory<P, G4Mag_EqRhs*(G4MagneticField*)> : public FF1<P,G4Mag_EqRhs*,G4MagneticField*> {};

  /// Factory to create Geant4 Processes
  template <typename P> class Factory<P, G4VProcess*()> : public FF0<P,G4VProcess*> {};
  /// Factory to create Geant4 Processes
  template <typename P> class Factory<P, G4VPhysicsConstructor*()> : public FF0<P,G4VPhysicsConstructor*> {};

  template <typename P> class Factory<P, G4ParticleDefinition*()>  { public:
    static void Func(void *ret, void*, const std::vector<void*>&, void*) { *(G4ParticleDefinition**)ret = P::Definition(); }
  };
  template <typename P> class Factory<P, long()>  { public:
    static void Func(void *ret, void*, const std::vector<void*>&, void*) { P::ConstructParticle(); *(long*)ret = 1L; }
  };
}

#define DECLARE_EXTERNAL_GEANT4SENSITIVEDETECTOR(name,func) \
  namespace DD4hep { namespace Simulation { struct external_geant4_sd_##name {}; \
      template <> G4VSensitiveDetector* Geant4SensitiveDetectorFactory< external_geant4_sd_##name >::create(const std::string& n,DD4hep::Geometry::LCDD& l) { return func(n,l); } \
    }}  using DD4hep::Simulation::external_geant4_sd_##name; \
  PLUGINSVC_FACTORY_WITH_ID(external_geant4_sd_##name,std::string(#name),G4VSensitiveDetector*(std::string,DD4hep::Geometry::LCDD*))

/// Plugin definition to create Geant4 sensitive detectors
#define DECLARE_GEANT4SENSITIVEDETECTOR(name) namespace DD4hep { namespace Simulation { struct geant4_sd_##name {}; \
      template <> G4VSensitiveDetector* Geant4SensitiveDetectorFactory< geant4_sd_##name >::create(const std::string& n,DD4hep::Geometry::LCDD& l) { return new name(n,l); } \
    }}  using DD4hep::Simulation::geant4_sd_##name; \
  PLUGINSVC_FACTORY_WITH_ID(geant4_sd_##name,std::string(#name),G4VSensitiveDetector*(std::string,DD4hep::Geometry::LCDD*))
#if 0     /* Equivalent:  */
#define DECLARE_GEANT4SENSITIVEDETECTOR(name)			\
  namespace DD4hep { namespace Simulation {  static G4VSensitiveDetector* __sd_create__##name(const std::string& n,DD4hep::Geometry::LCDD& p) {  return new name(n,p); } }} \
  DECLARE_EXTERNAL_GEANT4SENSITIVEDETECTOR(name,DD4hep::Simulation::__sd_create__##name)
#endif

#define DECLARE_GEANT4SENSITIVE(name) namespace DD4hep { namespace Simulation { }}  using DD4hep::Simulation::name; \
  PLUGINSVC_FACTORY_WITH_ID(name,std::string(#name),DD4hep::Simulation::Geant4Sensitive*(DD4hep::Simulation::Geant4Context*,std::string,DD4hep::Geometry::DetElement*,DD4hep::Geometry::LCDD*))

/// Plugin defintion to create Geant4Action objects
#define DECLARE_GEANT4ACTION(name) namespace DD4hep { namespace Simulation { }}  using DD4hep::Simulation::name; \
  PLUGINSVC_FACTORY_WITH_ID(name,std::string(#name),DD4hep::Simulation::Geant4Action*(DD4hep::Simulation::Geant4Context*,std::string))
/// Plugin definition to create Geant4 stepper objects
#define DECLARE_GEANT4_STEPPER(name)    PLUGINSVC_FACTORY_WITH_ID(G4##name,std::string(#name),G4MagIntegratorStepper*(G4EquationOfMotion*))
#define DECLARE_GEANT4_MAGSTEPPER(name) PLUGINSVC_FACTORY_WITH_ID(G4##name,std::string(#name),G4MagIntegratorStepper*(G4Mag_EqRhs*))
/// Plugin definition to create Geant4 equations of motion for magnetic fields
#define DECLARE_GEANT4_MAGMOTION(name) PLUGINSVC_FACTORY_WITH_ID(G4##name,std::string(#name),G4Mag_EqRhs*(G4MagneticField*))
/// Plugin definition to create Geant4 physics processes (G4VProcess)
#define DECLARE_GEANT4_PROCESS(name) PLUGINSVC_FACTORY_WITH_ID(name,std::string(#name),G4VProcess*())
/// Plugin definition to create Geant4 physics constructors (G4VPhysicsConstructor)
#define DECLARE_GEANT4_PHYSICS(name) PLUGINSVC_FACTORY_WITH_ID(name,std::string(#name),G4VPhysicsConstructor*())
/// Plugin definition to force particle constructors for GEANT4 (G4ParticleDefinition)
#define DECLARE_GEANT4_PARTICLE(name) PLUGINSVC_FACTORY_WITH_ID(name,std::string(#name),G4ParticleDefinition*())
/// Plugin definition to force particle constructors for GEANT4 (G4XXXXConstructor)
#define DECLARE_GEANT4_PARTICLEGROUP(name) PLUGINSVC_FACTORY_WITH_ID(name,std::string(#name),long())

#define DECLARE_GEANT4_SETUP(name,func) \
  namespace DD4hep { namespace Simulation { struct xml_g4_setup_##name {};  \
  template <> long Geant4SetupAction<DD4hep::Simulation::xml_g4_setup_##name>::create(LCDD& l,const DD4hep::Simulation::Geant4Converter& e, const std::map<std::string,std::string>& a) {return func(l,e,a);} }} \
  PLUGINSVC_FACTORY_WITH_ID(xml_g4_setup_##name,std::string(#name "_Geant4_action"),long(DD4hep::Geometry::LCDD*,const DD4hep::Simulation::Geant4Converter*,const std::map<std::string,std::string>*))
#endif // DDG4_FACTORIES_H
