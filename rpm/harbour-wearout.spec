Name:       harbour-wearout
Summary:    Experimental Wear OS watch bridge for Sailfish OS
Version:    0.1.0
Release:    1
License:    Apache-2.0
URL:        https://github.com/IsaakRichards/WearOut
Source0:    %{name}-%{version}.tar.bz2

BuildRequires: cmake
BuildRequires: pkgconfig(Qt5Core)
BuildRequires: pkgconfig(Qt5DBus)
BuildRequires: pkgconfig(Qt5Quick)
BuildRequires: pkgconfig(Qt5Qml)
BuildRequires: pkgconfig(sailfishapp) >= 1.0.2
Requires:   sailfishsilica-qt5 >= 0.10.9
Requires:   nemo-qml-plugin-dbus-qt5
Requires:   bluez5

%description
WearOut speaks the Wear OS "Wearable" Bluetooth protocol natively, so a
Wear OS watch can talk to a Sailfish OS phone without Android. Early,
experimental, reverse-engineering stage.

%prep
%setup -q -n %{name}-%{version}

%build
%cmake -DWEAROUT_BUILD_SAILFISH=ON -DWEAROUT_BUILD_CLI=OFF -DWEAROUT_BUILD_TESTS=OFF
%make_build

%install
%make_install

%post
systemctl-user daemon-reload || :

%files
%defattr(-,root,root,-)
%{_bindir}/wearoutd
%{_bindir}/harbour-wearout
%{_datadir}/harbour-wearout
%{_datadir}/applications/harbour-wearout.desktop
/usr/lib/systemd/user/wearoutd.service
