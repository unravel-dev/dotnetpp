#pragma once

/*
 * dotnetpp - unified .NET embedding API.
 *
 * Backend is selected at compile time. Exactly one of the following must be
 * defined to 1 (normally done by the dotnetpp CMake target via the
 * DOTNETPP_BACKEND cache variable):
 *
 *   DOTNETPP_BACKEND_MONO    - map dotnet:: onto monopp   (mono::)
 *   DOTNETPP_BACKEND_CORECLR - map dotnet:: onto clrpp    (clr::)
 */

#if !defined(DOTNETPP_BACKEND_MONO) && !defined(DOTNETPP_BACKEND_CORECLR)
// Default to the mono backend when nothing was selected.
#define DOTNETPP_BACKEND_MONO 1
#endif

#ifndef DOTNETPP_BACKEND_MONO
#define DOTNETPP_BACKEND_MONO 0
#endif

#ifndef DOTNETPP_BACKEND_CORECLR
#define DOTNETPP_BACKEND_CORECLR 0
#endif

#if DOTNETPP_BACKEND_MONO && DOTNETPP_BACKEND_CORECLR
#error "dotnetpp: only one backend may be selected"
#endif

#if !DOTNETPP_BACKEND_MONO && !DOTNETPP_BACKEND_CORECLR
#error "dotnetpp: no backend selected"
#endif

/*
 * dotnetpp_backend macro is defined in dotnet_managed.h after the active
 * backend headers are included. Use it for converter specializations
 * (namespace dotnetpp_backend::managed_interface); use dotnet:: elsewhere.
 */
