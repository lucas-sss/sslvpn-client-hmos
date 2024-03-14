import vpnExt from '@ohos.net.vpnExtension';
import Want from '@ohos.app.ability.Want';
import common from '@ohos.app.ability.common';
import VpnExtensionAbility from '@ohos.app.ability.VpnExtensionAbility';
import vpn_client from 'libvpn_client.so';
import hilog from '@ohos.hilog';

let g_tunnelFd = -1;
export default class VpnExtAbility extends VpnExtensionAbility {
  private VpnConnection: vpnExt.VpnConnection;
  private tunIp: string = '10.0.0.5';
  private blockedAppName: string = 'com.flksec.sslvpn.client.hmos';
  private vpnServerIp: string = '192.168.31.13';

  onCreate(want: Want) {
    this.VpnConnection = vpnExt.createVpnConnection(this.context);
    console.info("vpn createVpnConnection: " + JSON.stringify(this.VpnConnection));
    this.SetupVpn();
  }
  CreateTunnel() {
    g_tunnelFd = vpn_client.tcpConnect(this.vpnServerIp, 8888);
  }
  SetupVpn() {
    class Address {
      address: string;
      family: number;

      constructor(address: string, family: number) {
        this.address = address;
        this.family = family;
      }
    }

    class AddressWithPrefix {
      address: Address;
      prefixLength: number;

      constructor(address: Address, prefixLength: number) {
        this.address = address;
        this.prefixLength = prefixLength;
      }
    }

    class Config {
      addresses: AddressWithPrefix[];
      mtu: number;
      dnsAddresses: string[];
      trustedApplications: string[];
      blockedApplications: string[];

      constructor(
        tunIp: string,
        blockedAppName: string
      ) {
        this.addresses = [
          new AddressWithPrefix(new Address(tunIp, 1), 24)
        ];
        this.mtu = 1400;
        this.dnsAddresses = ["114.114.114.114"];
        this.trustedApplications = [];
        this.blockedApplications = [blockedAppName];
      }
    }

    let config = new Config(this.tunIp, this.blockedAppName);

    try {
      this.VpnConnection.create(config)
    } catch (error) {
      hilog.error(0x0000, 'developTag', 'vpn setUp fail: %{public}s', JSON.stringify(error) ?? '');
    }
  }
}