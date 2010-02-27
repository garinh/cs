/*
  Copyright (C) 2008 by Michael Gist

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
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

#ifndef __CS_CSUTIL_THREADING_PTHREAD_TLS_H__
#define __CS_CSUTIL_THREADING_PTHREAD_TLS_H__

#ifndef DOXYGEN_RUN

#include <pthread.h>

namespace CS
{
  namespace Threading
  {
    namespace Implementation
    {
      template<typename T>
      class ThreadLocalBase
      {
      public:
        ThreadLocalBase()
        {
          pthread_key_create(&threadIndex, NULL);
        }

        virtual ~ThreadLocalBase()
        {
          pthread_key_delete(threadIndex);
        }

        virtual void SetValue(T data)
        {
          pthread_setspecific(threadIndex, reinterpret_cast<void*>(data));
        }

        T GetValue() const
        {
          return reinterpret_cast<T>(pthread_getspecific(threadIndex));
        }

      protected:
        pthread_key_t threadIndex;
      };
    }
  } // Threading
} // CS

#endif // DOXYGEN_RUN

#endif // __CS_CSUTIL_THREADING_PTHREAD_TLS_H__
