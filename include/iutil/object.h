/*
    Copyright (C) 2000 by Jorrit Tyberghein

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

#ifndef __CS_IOBJECT_OBJECT_H__
#define __CS_IOBJECT_OBJECT_H__

/**\file
 * Generic object interface
 */
/**\addtogroup util
 * @{ */
#include "csutil/scf.h"
#include "csutil/scf_interface.h"
#include "csutil/ref.h"

struct iObjectIterator;
struct iObject;

/**
 * A callback that you can implement to get notified of name changes
 * in an iObject.
 */
struct iObjectNameChangeListener : public virtual iBase
{
  SCF_INTERFACE (iObjectNameChangeListener, 0, 0, 1);

  /// Object name has changed.
  virtual void NameChanged (iObject* obj, const char* oldname,
  	const char* newname) = 0;
};


/**
 * This interface is an SCF interface for encapsulating csObject.
 *
 * Main creators of instances implementing this interface:
 * - Many objects implement this (especially objects in the
 *   3D engine like meshes, lights, sectors, materials, ...)
 *
 * Main ways to get pointers to this interface:
 * - Many objects have a QueryObject() method that you can use.
 * - scfQueryInterface() on the object.
 * - iObject::GetObjectParent()
 * - iObject::GetChild()
 * - iObjectIterator::Next()
 * - iObjectIterator::GetParentObj()
 */
struct iObject : public virtual iBase
{
  SCF_INTERFACE(iObject,2,0,2);

  /// Set object name
  virtual void SetName (const char *iName) = 0;

  /// Query object name
  virtual const char *GetName () const = 0;

  /// Get the unique ID associated with this object.
  virtual uint GetID () const = 0;

  /**
   * Set the parent iObject. Note that this only sets the 'parent' pointer but
   * does not add the object as a child object.
   */
  virtual void SetObjectParent (iObject *obj) = 0;

  /// Returns the parent iObject.
  virtual iObject* GetObjectParent () const = 0;

  /// Attach a new iObject to the tree
  virtual void ObjAdd (iObject *obj) = 0;

  /// Remove an iObject from the tree.
  virtual void ObjRemove (iObject *obj) = 0;

  /// Remove all child objects.
  virtual void ObjRemoveAll () = 0;

  /// Add all child objects of the given object
  virtual void ObjAddChildren (iObject *Parent) = 0;

  /**
   * Look for a child object that implements the given interface. You can
   * optionally pass a name to look for. If FirstName is true then the
   * method will stop at the first object with the requested name, even
   * if it did not implement the requested type. Note that the returned
   * object must still be queried for the requested type. <p>
   *
   * \deprecated Deprecated in 1.3. Use GetChild(const char*) if you need
   *   "first" functionality, GetChild(int, int, const char*) otherwise.
   */
  CS_DEPRECATED_METHOD_MSG("Use GetChild(const char*) if you need \"first\" "
    "functionality, GetChild(int, int, const char*) otherwise.")
  virtual iObject* GetChild (int iInterfaceID, int iVersion,
    const char *Name, bool FirstName) const = 0;

  /// Return the first child object with the given name
  virtual iObject* GetChild (const char *Name) const = 0;

  /**
   * Return an iterator for all child objects. Note that you should not
   * remove child objects while iterating.
   */
  virtual csPtr<iObjectIterator> GetIterator () = 0;

  /// \todo Investigate a way to remove this function.
  virtual void ObjReleaseOld (iObject *obj) = 0;

  /**
   * Add a name change listener.
   */
  virtual void AddNameChangeListener (
  	iObjectNameChangeListener* listener) = 0;

  /**
   * Remove a name change listener.
   */
  virtual void RemoveNameChangeListener (
  	iObjectNameChangeListener* listener) = 0;
  
  /**
   * Look for a child object that implements the given interface. You can
   * optionally pass a name to look for.
   */
  virtual iObject* GetChild (int iInterfaceID, int iVersion,
    const char *Name = 0) const = 0;

};


/**
 * This is an iterator for child objects of a csObject. Note that this
 * iterator only contains type-independent functionality and is therefore
 * a bit complicated to use (i.e. you'll have to do a lot of
 * scfQueryInterface calls if you use it directly). Check out typed object
 * iterators instead.
 * 
 * Main creators of instances implementing this interface:
 * - iObject::GetIterator()
 */
struct iObjectIterator : public virtual iBase
{
  SCF_INTERFACE(iObjectIterator,2,0,0);
  /// Move forward
  virtual iObject* Next () = 0;

  /// Reset the iterator to the beginning
  virtual void Reset () = 0;

  /// Get the parent object
  virtual iObject* GetParentObj () const = 0;

  /// Check if we have any more children of requested type
  virtual bool HasNext () const = 0;

  /**
   * traverses all csObjects and looks for an object with the given name
   * returns object if found.
   * You can continue search by calling Next and
   * then do an other FindName, if you like.
   */
  virtual iObject* FindName (const char* name) = 0;
};

namespace CS
{
  /**
   * Get a child from an object that implements a specific interface.
   */
  template<typename Interface>
  static inline csPtr<Interface> GetChildObject (iObject* object)
  {
    return scfQueryInterfaceSafe<Interface> (object->GetChild (
      scfInterfaceTraits<Interface>::GetID(),
      scfInterfaceTraits<Interface>::GetVersion()));
  }

  /**
   * Get a child from an object that has the given name and implements a 
   * specific interface.
   */
  template<typename Interface>
  static inline csPtr<Interface> GetNamedChildObject (iObject* object,
                                                      const char* name)
  {
    return scfQueryInterfaceSafe<Interface> (object->GetChild (scfInterfaceTraits<Interface>::GetID(),
                                             scfInterfaceTraits<Interface>::GetVersion(), name));
  }
}

template<typename Interface>
inline CS_DEPRECATED_METHOD_MSG ("CS_GET_CHILD_OBJECT macro is deprecated, "
                                 "use CS::GetChildObject() instead")
csPtr<Interface> CS_GET_CHILD_OBJECT_is_deprecated (iObject* Object)
{
  return CS::GetChildObject<Interface> (Object);
}
/**
 * \deprecated Compatibility macro. Use CS::GetChildObject() instead
 * \sa CS::GetChildObject
 */
#define CS_GET_CHILD_OBJECT(Object, Interface) \
  (CS_GET_CHILD_OBJECT_is_deprecated<Interface> (Object))

template<typename Interface>
inline CS_DEPRECATED_METHOD_MSG ("CS_GET_NAMED_CHILD_OBJECT macro is deprecated, "
                                 "use CS::GetNamedChildObject() instead")
csPtr<Interface> CS_GET_NAMED_CHILD_OBJECT_is_deprecated (iObject* Object,
  const char* Name)
{
  return CS::GetNamedChildObject<Interface> (Object, Name);
}
/**
 * \deprecated Compatibility macro. Use iObject->GetChild() and 
 *   scfQueryInterface().
 * \sa iObject::GetChild
 */
#define CS_GET_NAMED_CHILD_OBJECT(Object, Interface, Name) \
  (CS_GET_NAMED_CHILD_OBJECT_is_deprecated<Interface> (Object, Name))

template<typename Interface>
inline CS_DEPRECATED_METHOD_MSG ("CS_GET_FIRST_NAMED_CHILD_OBJECT macro is deprecated, "
                                 "use iObject->GetChild() and scfQueryInterface() ")
csPtr<Interface> CS_GET_FIRST_NAMED_CHILD_OBJECT_is_deprecated (iObject* Object,
  const char* Name)
{
  return scfQueryInterfaceSafe<Interface> (Object->GetChild (Name));
}
/**
 * \deprecated Compatibility macro. Use CS::GetNamedChildObject() instead
 * \sa CS::GetNamedChildObject
 */
#define CS_GET_FIRST_NAMED_CHILD_OBJECT(Object, Interface, Name) \
  (CS_GET_FIRST_NAMED_CHILD_OBJECT_is_deprecated<Interface> (Object, Name))

/** @} */

#endif // __CS_IOBJECT_OBJECT_H__
