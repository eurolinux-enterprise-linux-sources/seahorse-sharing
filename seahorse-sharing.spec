Name:           seahorse-sharing
Version:        3.8.0
Release:        1%{?dist}
Summary:        Sharing of PGP public keys via DNS-SD and HKP
# daemon is GPLv2+
# libegg is LGPLv2+
License:        GPLv2+ and LGPLv2+
URL:            https://live.gnome.org/Seahorse
Source0:        http://ftp.gnome.org/pub/gnome/sources/seahorse-sharing/3.8/%{name}-%{version}.tar.xz

Provides:       bundled(egglib)

BuildRequires:  gtk3-devel
BuildRequires:  desktop-file-utils
BuildRequires:  gnupg2
BuildRequires:  gpgme-devel >= 1.0
BuildRequires:  libsoup-devel
BuildRequires:  avahi-glib-devel
BuildRequires:  intltool
BuildRequires:  libSM-devel

Obsoletes: seahorse < 3.1.4

%description
This package ships a session daemon that allows users to share PGP public keys
via DNS-SD and HKP.


%prep
%setup -q


%build
%configure
make %{?_smp_mflags}


%install
make install DESTDIR=$RPM_BUILD_ROOT

desktop-file-validate $RPM_BUILD_ROOT%{_sysconfdir}/xdg/autostart/%{name}.desktop

%find_lang %{name} --with-gnome


%files -f %{name}.lang
%doc AUTHORS COPYING NEWS README
%{_sysconfdir}/xdg/autostart/%{name}.desktop
%{_bindir}/%{name}
%{_datadir}/pixmaps/seahorse/
%{_mandir}/man1/%{name}.1.gz


%changelog
* Tue Mar 26 2013 Kalev Lember <kalevlember@gmail.com> - 3.8.0-1
- Update to 3.8.0

* Wed Feb 06 2013 Kalev Lember <kalevlember@gmail.com> - 3.7.5-1
- Update to 3.7.5

* Tue Nov 13 2012 Kalev Lember <kalevlember@gmail.com> - 3.6.1-1
- Update to 3.6.1

* Wed Sep 26 2012 Kalev Lember <kalevlember@gmail.com> - 3.6.0-1
- Update to 3.6.0

* Wed Sep 19 2012 Kalev Lember <kalevlember@gmail.com> - 3.5.92-1
- Update to 3.5.92

* Sat Jul 21 2012 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 3.4.0-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_18_Mass_Rebuild

* Tue Mar 27 2012 Rui Matos <rmatos@redhat.com> - 3.4.0-1
- Update to 3.4.0
- Added Provides: bundled(egglib)

* Mon Mar 19 2012 Rui Matos <rmatos@redhat.com> - 3.3.92-1
- Update to 3.3.92
- Don't ship MAINTAINERS
- Own %%{_datadir}/pixmaps/seahorse/

* Tue Mar  6 2012 Rui Matos <rmatos@redhat.com> - 3.2.1-1
- initial packaging for Fedora

