/*
    Copyright (C) 2001 by Jorrit Tyberghein

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef __CS_IUTIL_OBJREG_H__
#define __CS_IUTIL_OBJREG_H__

/**\file
 * Object registry interface
 */
 
/**
 * \addtogroup scf
 * @{ */

#include "csutil/ref.h"
#include "csutil/scf_interface.h"

struct iObjectRegistryIterator;

/**
 * This interface serves as a registry of other objects.
 * The object registry is thread-safe.
 *
 * Main creators of instances implementing this interface:
 * - csInitializer::CreateObjectRegistry()
 * 
 * Main ways to get pointers to this interface:
 * - Stored by application.
 * 
 * Main users of this interface:
 * - Everything.
 */
struct iObjectRegistry : public virtual iBase
{
  SCF_INTERFACE(iObjectRegistry, 2,0,0);
  // Allow implicit casts through static function.
  CS_IMPLEMENT_IMPLICIT_PTR_CAST (iObjectRegistry);

  /**
   * Clear the object registry and release all references.
   */
  virtual void Clear () = 0;

  /**
   * Register an object with this registry. The same object can
   * be registered multiple times but in that case it is probably
   * best to have different tags so they can be distinguished.
   * \param obj Object to place into the registry.
   * \param tag Optional tag. The tag is interpreted in any way the client sees
   *   fit, however some facilities expect tags to follow a certain
   *   convention, so be sure to check suitable documentation.
   * \return False if the given tag is already registered; if obj is null; or
   *   if the registry is in the process of being cleared.
   * \remarks The reference count of obj will be incremented at registration
   *   time.
   * <p>
   * \remarks A particular tag can only be registered once. If you need to
   *   re-use a tag, first unregister the object to which it is associated.
   */
  virtual bool Register (iBase* obj, char const* tag = 0) = 0;

  /**
   * Unregister an object with this registry.
   * \param obj Object to removed from the registry.
   * \param tag Optional tag.
   * \remarks If tag is not provided, * then it will unregister all occurrences
   *   of obj * in the registry (i.e. for all tags). If tag is provided, then
   *   only * the particular registration of obj associated with tag will be
   *   unregistered.
   * <p>
   * \remarks The reference count of obj will be decremented at removal time.
   */
  virtual void Unregister (iBase* obj, char const* tag = 0) = 0;

  /**
   * Get the registered object corresponding with the given tag.
   * \remarks The reference count of the returned object will be artifically
   *   incremented, so clients must take care to decrement the reference when
   *   no longer needed.
   */
  virtual iBase* Get (char const* tag) = 0;

  /**
   * Get the registered object corresponding with the given tag and
   * implementing the specified interface.
   * \remarks The reference count of the returned object will be artifically
   *   incremented, so clients must take care to decrement the reference when
   *   no longer needed.
   */
  virtual iBase* Get (char const* tag, scfInterfaceID id, int version) = 0;

  /**
   * Get an iterator with all objects implementing the given interface.
   * Note that the iterator iterates over a copy of the elements in the
   * object registry so no thread-locking on the object registry happens except
   * at the time the iterator is created.
   */
  virtual csPtr<iObjectRegistryIterator> Get (
  	scfInterfaceID id, int version) = 0;

  /**
   * Get an iterator with all objects in this object registry.
   * Note that the iterator iterates over a copy of the elements in the
   * object registry so no thread-locking on the object registry happens except
   * at the time the iterator is created.
   */
  virtual csPtr<iObjectRegistryIterator> Get () = 0;
};


/**
 * Use an instance of this class to iterate over objects in the object
 * registry.
 */
struct iObjectRegistryIterator : public virtual iBase
{
  SCF_INTERFACE(iObjectRegistryIterator, 2,0,0);
  /**
   * Restart the iterator. Returns false if there are no ellements
   * in it.
   */
  virtual bool Reset () = 0;

  /**
   * Return the current tag.
   */
  virtual const char* GetCurrentTag () = 0;

  /**
   * Return true if there are more elements.
   */
  virtual bool HasNext () = 0;

  /**
   * Proceed with next element. Return the element is there is one.
   */
  virtual iBase* Next () = 0;
};

/** @} */

/**
 * Query an interface from the registry. The tag is the name of the interface.
 */
template<class Interface>
inline csPtr<Interface> csQueryRegistry (iObjectRegistry *Reg)
{ 
  iBase *base = Reg->Get (scfInterfaceTraits<Interface>::GetName (),
              scfInterfaceTraits<Interface>::GetID (),
              scfInterfaceTraits<Interface>::GetVersion ());

  if (base == 0) return csPtr<Interface> (0);

  Interface *x = (Interface*)base->QueryInterface (
    scfInterfaceTraits<Interface>::GetID (),
    scfInterfaceTraits<Interface>::GetVersion ());

  if (x) base->DecRef (); //release our base interface
  return csPtr<Interface> (x);
}  
  
/**
 * Query an object from the registry, with a tag specified by the user.
 */
inline csPtr<iBase> csQueryRegistryTag (iObjectRegistry *Reg, const char* Tag)
{  
  return csPtr<iBase> (Reg->Get (Tag));
}  
   
/**
 * Query an interface from the registry, with a tag specified by the user.
 */
template<class Interface>
inline csPtr<Interface> csQueryRegistryTagInterface (
  iObjectRegistry *Reg, const char* Tag)
{  
  iBase *base = Reg->Get (Tag,
              scfInterfaceTraits<Interface>::GetID (),
              scfInterfaceTraits<Interface>::GetVersion ());

  if (base == 0) return csPtr<Interface> (0);

  Interface *x = (Interface*)base->QueryInterface (
    scfInterfaceTraits<Interface>::GetID (),
    scfInterfaceTraits<Interface>::GetVersion ());

  if (x) base->DecRef (); //release our base interface
  return csPtr<Interface> (x);
}  

#endif // __CS_IUTIL_OBJREG_H__

