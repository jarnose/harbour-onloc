# The background service, packaged separately from the application. See
# docs/daemon.md. Built from the repository root with:
#
#   sfdk -c specfile=daemon/onloc-daemon.spec build
#
# Not in rpm/: sfdk refuses to build when it finds two spec files there, and
# Qt Creator passes no specfile option (the same arrangement as
# harbour-suuntosync's daemon).

Name:       onloc-daemon
Summary:    OnLoc background service
Version:    1.1.0
Release:    1
License:    MIT
URL:        https://github.com/onloc-app
Source0:    %{name}-%{version}.tar.bz2

# Deliberately no Requires: harbour-onloc - an RPM dependency would make
# removing the application from the app grid fail without explanation.
Requires:   systemd-user-session-targets

BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(Qt5Positioning)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  cmake

%description
Keeps reporting this phone's position to an OnLoc server after the OnLoc
application is closed, and carries out the server's ring, lock and flash
commands. It is a systemd user service with no interface of its own; the
application hands it a sign-in and shows its status.

It is a separate package because it cannot run inside the Sailjail sandbox: it
has to outlive the application, play sound and drive the display. Removing it
leaves the application working on its own.

%prep
%setup -q -n %{name}-%{version}

%build
%cmake -DBUILD_APP=OFF -DBUILD_DAEMON=ON
%make_build

%install
%make_install

%files
%defattr(-,root,root,-)
%{_bindir}/onlocd
%{_prefix}/lib/systemd/user/onlocd.service

%post
systemctl-user daemon-reload || :
systemctl-user enable onlocd.service || :
# restart, not "enable --now": --now leaves an already running service on its
# old binary after an upgrade; restart starts it when stopped and replaces it
# when running.
systemctl-user restart onlocd.service || :

%preun
if [ "$1" = 0 ]; then
    systemctl-user disable --now onlocd.service || :
fi

%postun
systemctl-user daemon-reload || :
