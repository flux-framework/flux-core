Summary:	High-level C binding for 0MQ
Name:		czmq
Version:	2.2.0
Release:	3
License:	LGPL v3+
Group:		Libraries
Source0:	http://download.zeromq.org/%{name}-%{version}.tar.gz
# Source0-md5:	a83cbea8c2687813e1343d711589ec15
URL:		http://zeromq.org/
BuildRequires:	asciidoc
BuildRequires:	libsodium-devel
BuildRequires:	xmlto
BuildRequires:	zeromq4-devel
BuildRequires:	libuuid-devel
Requires:	zeromq4
BuildRoot:	%{tmpdir}/%{name}-%{version}-root-%(id -u -n)

%description
High-level C binding for 0MQ.

%package devel
Summary:	Header files for CZMQ library
Group:		Development/Libraries
Requires:	%{name} = %{version}-%{release}
Requires:	zeromq4-devel

%description devel
Header files for CZMQ library.

%prep
%setup -q

%build
# use include subdir - file names could be too common (zfile.h etc.)
%configure \
	--includedir=%{_includedir}/czmq \
	--disable-static
%{__make}

%install
rm -rf $RPM_BUILD_ROOT

%{__make} install \
	DESTDIR=$RPM_BUILD_ROOT

# obsoleted by pkg-config
%{__rm} $RPM_BUILD_ROOT%{_libdir}/libczmq.la

# too common name
%{__mv} $RPM_BUILD_ROOT%{_bindir}/{makecert,czmq_makecert}

# this looks like a Makefile.am problem -jg
%{__rm} -f $RPM_BUILD_ROOT%{_bindir}/*.gsl

%clean
rm -rf $RPM_BUILD_ROOT

%post -p /sbin/ldconfig


%postun -p /sbin/ldconfig


%files
%defattr(644,root,root,755)
%doc AUTHORS NEWS
%attr(755,root,root) %{_bindir}/czmq_makecert
%attr(755,root,root) %{_bindir}/czmq_selftest
%attr(755,root,root) %{_libdir}/libczmq.so.*.*.*
%attr(755,root,root) %ghost %{_libdir}/libczmq.so.1

%files devel
%defattr(644,root,root,755)
%attr(755,root,root) %{_libdir}/libczmq.so
%{_includedir}/czmq
%{_libdir}/pkgconfig/libczmq.pc
%{_mandir}/man3/*.3*
%{_mandir}/man7/*.7*

%changelog
* Thu May 08 2014 Jim Garlick <garlick@llnl.gov> - 2.2.0-3
- buildrequire libuuid-devel
- disable static subpackage
- drop Polish (pl) descriptions that I can't keep up to date

* Wed May 07 2014 Jim Garlick <garlick@llnl.gov> - 2.2.0-2
- add ldconfig post/postun

* Wed May 07 2014 Jim Garlick <garlick@llnl.gov> - 2.2.0-1
- drop unpackaged *.gsl files from bindir
- initial build for TOSS
