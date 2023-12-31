PR_GetProtoByNumber
===================

Looks up a protocol entry based on protocol's number.


Syntax
------

.. code::

   #include <prnetdb.h>

   PRStatus PR_GetProtoByNumber(
     PRInt32 protocolnumber,
     char* buffer,
     PRInt32 bufsize,
     PRProtoEnt* result);


Parameters
~~~~~~~~~~

The function has the following parameters:

``protocolnumber``
   The number assigned to the protocol.
``buffer``
   A pointer to a scratch buffer for the runtime to return result. This
   buffer is allocated by the caller.
``bufsize``
   Number of bytes in the ``buffer`` parameter. The buffer must be at
   least :ref:`PR_NETDB_BUF_SIZE` bytes.
``result``
   On input, a pointer to a :ref:`PRNetAddr` structure. On output, this
   structure is filled in by the runtime if the function returns
   ``PR_SUCCESS``.


Returns
~~~~~~~

The function returns one of the following values:

-  If successful, ``PR_SUCCESS``.
-  If unsuccessful, ``PR_FAILURE``. You can retrieve the reason for the
   failure by calling :ref:`PR_GetError`.
