<?php
/*
 * certs.inc
 *
 * part of pfSense (https://www.pfsense.org)
 * Copyright (c) 2008-2016 Rubicon Communications, LLC (Netgate)
 * Copyright (c) 2008 Shrew Soft Inc. All rights reserved.
 * All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

define("OPEN_SSL_CONF_PATH", "/etc/ssl/openssl.cnf");

require_once("functions.inc");

global $openssl_digest_algs;
$openssl_digest_algs = array("sha1", "sha224", "sha256", "sha384", "sha512");

global $openssl_crl_status;
$openssl_crl_status = array(
	OCSP_REVOKED_STATUS_NOSTATUS              => "No Status (default)",
	OCSP_REVOKED_STATUS_UNSPECIFIED           => "Unspecified",
	OCSP_REVOKED_STATUS_KEYCOMPROMISE         => "Key Compromise",
	OCSP_REVOKED_STATUS_CACOMPROMISE          => "CA Compromise",
	OCSP_REVOKED_STATUS_AFFILIATIONCHANGED    => "Affiliation Changed",
	OCSP_REVOKED_STATUS_SUPERSEDED            => "Superseded",
	OCSP_REVOKED_STATUS_CESSATIONOFOPERATION  => "Cessation of Operation",
	OCSP_REVOKED_STATUS_CERTIFICATEHOLD       => "Certificate Hold"
);

global $cert_altname_types;
$cert_altname_types = array(
	'DNS' => gettext('FQDN or Hostname'),
	'IP' => gettext('IP address'),
	'URI' => gettext('URI'),
	'email' => gettext('email address'),
);


function & lookup_ca($refid) {
	global $config;

	if (is_array($config['ca'])) {
		foreach ($config['ca'] as & $ca) {
			if ($ca['refid'] == $refid) {
				return $ca;
			}
		}
	}

	return false;
}

function & lookup_ca_by_subject($subject) {
	global $config;

	if (is_array($config['ca'])) {
		foreach ($config['ca'] as & $ca) {
			$ca_subject = cert_get_subject($ca['crt']);
			if ($ca_subject == $subject) {
				return $ca;
			}
		}
	}

	return false;
}

function & lookup_cert($refid) {
	global $config;

	if (is_array($config['cert'])) {
		foreach ($config['cert'] as & $cert) {
			if ($cert['refid'] == $refid) {
				return $cert;
			}
		}
	}

	return false;
}

function & lookup_cert_by_name($name) {
	global $config;
	if (is_array($config['cert'])) {
		foreach ($config['cert'] as & $cert) {
			if ($cert['descr'] == $name) {
				return $cert;
			}
		}
	}
}

function & lookup_crl($refid) {
	global $config;

	if (is_array($config['crl'])) {
		foreach ($config['crl'] as & $crl) {
			if ($crl['refid'] == $refid) {
				return $crl;
			}
		}
	}

	return false;
}

function ca_chain_array(& $cert) {
	if ($cert['caref']) {
		$chain = array();
		$crt = lookup_ca($cert['caref']);
		$chain[] = $crt;
		while ($crt) {
			$caref = $crt['caref'];
			if ($caref) {
				$crt = lookup_ca($caref);
			} else {
				$crt = false;
			}
			if ($crt) {
				$chain[] = $crt;
			}
		}
		return $chain;
	}
	return false;
}

function ca_chain(& $cert) {
	if ($cert['caref']) {
		$ca = "";
		$cas = ca_chain_array($cert);
		if (is_array($cas)) {
			foreach ($cas as & $ca_cert) {
				$ca .= base64_decode($ca_cert['crt']);
				$ca .= "\n";
			}
		}
		return $ca;
	}
	return "";
}

function ca_import(& $ca, $str, $key = "", $serial = "") {
	global $config;

	$ca['crt'] = base64_encode($str);
	if (!empty($key)) {
		$ca['prv'] = base64_encode($key);
	}
	if (empty($serial)) {
		$ca['serial'] = 0;
	} else {
		$ca['serial'] = $serial;
	}
	$subject = cert_get_subject($str, false);
	$issuer = cert_get_issuer($str, false);

	// Find my issuer unless self-signed
	if ($issuer <> $subject) {
		$issuer_crt =& lookup_ca_by_subject($issuer);
		if ($issuer_crt) {
			$ca['caref'] = $issuer_crt['refid'];
		}
	}

	/* Correct if child certificate was loaded first */
	if (is_array($config['ca'])) {
		foreach ($config['ca'] as & $oca) {
			$issuer = cert_get_issuer($oca['crt']);
			if ($ca['refid'] <> $oca['refid'] && $issuer == $subject) {
				$oca['caref'] = $ca['refid'];
			}
		}
	}
	if (is_array($config['cert'])) {
		foreach ($config['cert'] as & $cert) {
			$issuer = cert_get_issuer($cert['crt']);
			if ($issuer == $subject) {
				$cert['caref'] = $ca['refid'];
			}
		}
	}
	return true;
}

function ca_create(& $ca, $keylen, $lifetime, $dn, $digest_alg = "sha256") {

	$args = array(
		"x509_extensions" => "v3_ca",
		"digest_alg" => $digest_alg,
		"private_key_bits" => (int)$keylen,
		"private_key_type" => OPENSSL_KEYTYPE_RSA,
		"encrypt_key" => false);

	// generate a new key pair
	$res_key = openssl_pkey_new($args);
	if (!$res_key) {
		return false;
	}

	// generate a certificate signing request
	$res_csr = openssl_csr_new($dn, $res_key, $args);
	if (!$res_csr) {
		return false;
	}

	// self sign the certificate
	$res_crt = openssl_csr_sign($res_csr, null, $res_key, $lifetime, $args);
	if (!$res_crt) {
		return false;
	}

	// export our certificate data
	if (!openssl_pkey_export($res_key, $str_key) ||
	    !openssl_x509_export($res_crt, $str_crt)) {
		return false;
	}

	// return our ca information
	$ca['crt'] = base64_encode($str_crt);
	$ca['prv'] = base64_encode($str_key);
	$ca['serial'] = 0;

	return true;
}

function ca_inter_create(& $ca, $keylen, $lifetime, $dn, $caref, $digest_alg = "sha256") {
	// Create Intermediate Certificate Authority
	$signing_ca =& lookup_ca($caref);
	if (!$signing_ca) {
		return false;
	}

	$signing_ca_res_crt = openssl_x509_read(base64_decode($signing_ca['crt']));
	$signing_ca_res_key = openssl_pkey_get_private(array(0 => base64_decode($signing_ca['prv']) , 1 => ""));
	if (!$signing_ca_res_crt || !$signing_ca_res_key) {
		return false;
	}
	$signing_ca_serial = ++$signing_ca['serial'];

	$args = array(
		"x509_extensions" => "v3_ca",
		"digest_alg" => $digest_alg,
		"private_key_bits" => (int)$keylen,
		"private_key_type" => OPENSSL_KEYTYPE_RSA,
		"encrypt_key" => false);

	// generate a new key pair
	$res_key = openssl_pkey_new($args);
	if (!$res_key) {
		return false;
	}

	// generate a certificate signing request
	$res_csr = openssl_csr_new($dn, $res_key, $args);
	if (!$res_csr) {
		return false;
	}

	// Sign the certificate
	$res_crt = openssl_csr_sign($res_csr, $signing_ca_res_crt, $signing_ca_res_key, $lifetime, $args, $signing_ca_serial);
	if (!$res_crt) {
		return false;
	}

	// export our certificate data
	if (!openssl_pkey_export($res_key, $str_key) ||
	    !openssl_x509_export($res_crt, $str_crt)) {
		return false;
	}

	// return our ca information
	$ca['crt'] = base64_encode($str_crt);
	$ca['prv'] = base64_encode($str_key);
	$ca['serial'] = 0;
	$ca['caref'] = $caref;

	return true;
}

function cert_import(& $cert, $crt_str, $key_str) {

	$cert['crt'] = base64_encode($crt_str);
	$cert['prv'] = base64_encode($key_str);

	$subject = cert_get_subject($crt_str, false);
	$issuer = cert_get_issuer($crt_str, false);

	// Find my issuer unless self-signed
	if ($issuer <> $subject) {
		$issuer_crt =& lookup_ca_by_subject($issuer);
		if ($issuer_crt) {
			$cert['caref'] = $issuer_crt['refid'];
		}
	}
	return true;
}

function cert_create(& $cert, $caref, $keylen, $lifetime, $dn, $type = "user", $digest_alg = "sha256") {

	$cert['type'] = $type;

	if ($type != "self-signed") {
		$cert['caref'] = $caref;
		$ca =& lookup_ca($caref);
		if (!$ca) {
			return false;
		}

		$ca_str_crt = base64_decode($ca['crt']);
		$ca_str_key = base64_decode($ca['prv']);
		$ca_res_crt = openssl_x509_read($ca_str_crt);
		$ca_res_key = openssl_pkey_get_private(array(0 => $ca_str_key, 1 => ""));
		if (!$ca_res_key) {
			return false;
		}
		if (empty($ca['serial'])) {
			$ca['serial'] = 0;
		}
		$ca_serial = ++$ca['serial'];
	}

	$cert_type = cert_type_config_section($type);

	// in case of using Subject Alternative Names use other sections (with postfix '_san')
	// pass subjectAltName over environment variable 'SAN'
	if ($dn['subjectAltName']) {
		putenv("SAN={$dn['subjectAltName']}"); // subjectAltName can be set _only_ via configuration file
		$cert_type .= '_san';
		unset($dn['subjectAltName']);
	}

	$args = array(
		"x509_extensions" => $cert_type,
		"digest_alg" => $digest_alg,
		"private_key_bits" => (int)$keylen,
		"private_key_type" => OPENSSL_KEYTYPE_RSA,
		"encrypt_key" => false);

	// generate a new key pair
	$res_key = openssl_pkey_new($args);
	if (!$res_key) {
		return false;
	}

	// If this is a self-signed cert, blank out the CA and sign with the cert's key
	if ($type == "self-signed") {
		$ca           = null;
		$ca_res_crt   = null;
		$ca_res_key   = $res_key;
		$ca_serial    = 0;
		$cert['type'] = "server";
	}

	// generate a certificate signing request
	$res_csr = openssl_csr_new($dn, $res_key, $args);
	if (!$res_csr) {
		return false;
	}

	// sign the certificate using an internal CA
	$res_crt = openssl_csr_sign($res_csr, $ca_res_crt, $ca_res_key, $lifetime,
				 $args, $ca_serial);
	if (!$res_crt) {
		return false;
	}

	// export our certificate data
	if (!openssl_pkey_export($res_key, $str_key) ||
	    !openssl_x509_export($res_crt, $str_crt)) {
		return false;
	}

	// return our certificate information
	$cert['crt'] = base64_encode($str_crt);
	$cert['prv'] = base64_encode($str_key);

	return true;
}

function csr_generate(& $cert, $keylen, $dn, $type = "user", $digest_alg = "sha256") {

	$cert_type = cert_type_config_section($type);

	// in case of using Subject Alternative Names use other sections (with postfix '_san')
	// pass subjectAltName over environment variable 'SAN'
	if ($dn['subjectAltName']) {
		putenv("SAN={$dn['subjectAltName']}"); // subjectAltName can be set _only_ via configuration file
		$cert_type .= '_san';
		unset($dn['subjectAltName']);
	}

	$args = array(
		"x509_extensions" => $cert_type,
		"req_extensions" => "req_{$cert_type}",
		"digest_alg" => $digest_alg,
		"private_key_bits" => (int)$keylen,
		"private_key_type" => OPENSSL_KEYTYPE_RSA,
		"encrypt_key" => false);

	// generate a new key pair
	$res_key = openssl_pkey_new($args);
	if (!$res_key) {
		return false;
	}

	// generate a certificate signing request
	$res_csr = openssl_csr_new($dn, $res_key, $args);
	if (!$res_csr) {
		return false;
	}

	// export our request data
	if (!openssl_pkey_export($res_key, $str_key) ||
	    !openssl_csr_export($res_csr, $str_csr)) {
		return false;
	}

	// return our request information
	$cert['csr'] = base64_encode($str_csr);
	$cert['prv'] = base64_encode($str_key);

	return true;
}

function csr_sign($csr, & $ca, $duration, $type = "user", $altnames) {
	global $config;
	$old_err_level = error_reporting(0);

	// Gather the information required for signed cert
	$ca_str_crt = base64_decode($ca['crt']);
	$ca_str_key = base64_decode($ca['prv']);
	$ca_res_key = openssl_pkey_get_private(array(0 => $ca_str_key, 1 => ""));
	if (!$ca_res_key) {
		return false;
	}
	if (empty($ca['serial'])) {
		$ca['serial'] = 0;
	}
	$ca_serial = ++$ca['serial'];

	$cert_type = cert_type_config_section($type);

	if (!empty($altnames)) {
		putenv("SAN={$altnames}"); // subjectAltName can be set _only_ via configuration file
		$cert_type .= '_san';
	}

	$args = array(
		"digest_alg" => "sha256", // use sha256 digest until user interface allows specification
		"x509_extensions" => $cert_type,
		"req_extensions" => "req_{$cert_type}"
	);

	// Sign the new cert and export it in x509 format
	openssl_x509_export(openssl_csr_sign($csr, $ca_str_crt, $ca_str_key, $duration, $args, $ca_serial), $n509);
	error_reporting($old_err_level);

	return $n509;
}

function csr_complete(& $cert, $str_crt) {
	$str_key = base64_decode($cert['prv']);
	cert_import($cert, $str_crt, $str_key);
	unset($cert['csr']);
	return true;
}

function csr_get_subject($str_crt, $decode = true) {

	if ($decode) {
		$str_crt = base64_decode($str_crt);
	}

	$components = openssl_csr_get_subject($str_crt);

	if (empty($components) || !is_array($components)) {
		return "unknown";
	}

	ksort($components);
	foreach ($components as $a => $v) {
		if (!strlen($subject)) {
			$subject = "{$a}={$v}";
		} else {
			$subject = "{$a}={$v}, {$subject}";
		}
	}

	return $subject;
}

function cert_get_subject($str_crt, $decode = true) {

	if ($decode) {
		$str_crt = base64_decode($str_crt);
	}

	$inf_crt = openssl_x509_parse($str_crt);
	$components = $inf_crt['subject'];

	if (empty($components) || !is_array($components)) {
		return "unknown";
	}

	ksort($components);
	foreach ($components as $a => $v) {
		if (is_array($v)) {
			ksort($v);
			foreach ($v as $w) {
				$asubject = "{$a}={$w}";
				$subject = (strlen($subject)) ? "{$asubject}, {$subject}" : $asubject;
			}
		} else {
			$asubject = "{$a}={$v}";
			$subject = (strlen($subject)) ? "{$asubject}, {$subject}" : $asubject;
		}
	}

	return $subject;
}

function cert_get_subject_array($crt) {
	$str_crt = base64_decode($crt);
	$inf_crt = openssl_x509_parse($str_crt);
	$components = $inf_crt['subject'];

	if (!is_array($components)) {
		return;
	}

	$subject_array = array();

	foreach ($components as $a => $v) {
		$subject_array[] = array('a' => $a, 'v' => $v);
	}

	return $subject_array;
}

function cert_get_subject_hash($crt) {
	$str_crt = base64_decode($crt);
	$inf_crt = openssl_x509_parse($str_crt);
	return $inf_crt['subject'];
}

function cert_get_sans($str_crt, $decode = true) {
	if ($decode) {
		$str_crt = base64_decode($str_crt);
	}
	$sans = array();
	$crt_details = openssl_x509_parse($str_crt);
	if (!empty($crt_details['extensions']['subjectAltName'])) {
		$sans = explode(',', $crt_details['extensions']['subjectAltName']);
	}
	return $sans;
}

function cert_get_issuer($str_crt, $decode = true) {

	if ($decode) {
		$str_crt = base64_decode($str_crt);
	}

	$inf_crt = openssl_x509_parse($str_crt);
	$components = $inf_crt['issuer'];

	if (empty($components) || !is_array($components)) {
		return "unknown";
	}

	ksort($components);
	foreach ($components as $a => $v) {
		if (is_array($v)) {
			ksort($v);
			foreach ($v as $w) {
				$aissuer = "{$a}={$w}";
				$issuer = (strlen($issuer)) ? "{$aissuer}, {$issuer}" : $aissuer;
			}
		} else {
			$aissuer = "{$a}={$v}";
			$issuer = (strlen($issuer)) ? "{$aissuer}, {$issuer}" : $aissuer;
		}
	}

	return $issuer;
}

/* Works for both RSA and ECC (crt) and key (prv) */
function cert_get_publickey($str_crt, $decode = true, $type = "crt") {
	if ($decode) {
		$str_crt = base64_decode($str_crt);
	}
	switch ($type) {
		case 'prv':
			exec("echo \"{$str_crt}\" | openssl pkey -pubout", $out);
			break;
		case 'crt':
			exec("echo \"{$str_crt}\" | openssl x509 -inform pem -noout -pubkey", $out);
			break;
		case 'csr':
			exec("echo \"{$str_crt}\" | openssl req -inform pem -noout -pubkey", $out);
			break;
		default:
			$out = array();
			break;
	}
	return implode("\n", $out);
}

function cert_get_purpose($str_crt, $decode = true) {
	$extended_oids = array(
		"1.3.6.1.5.5.8.2.2" => "IP Security IKE Intermediate",
	);
	if ($decode) {
		$str_crt = base64_decode($str_crt);
	}
	$crt_details = openssl_x509_parse($str_crt);
	$purpose = array();
	if (!empty($crt_details['extensions']['keyUsage'])) {
		$purpose['ku'] = explode(',', $crt_details['extensions']['keyUsage']);
		foreach ($purpose['ku'] as & $ku) {
			$ku = trim($ku);
			if (array_key_exists($ku, $extended_oids)) {
				$ku = $extended_oids[$ku];
			}
		}
	} else {
		$purpose['ku'] = array();
	}
	if (!empty($crt_details['extensions']['extendedKeyUsage'])) {
		$purpose['eku'] = explode(',', $crt_details['extensions']['extendedKeyUsage']);
		foreach ($purpose['eku'] as & $eku) {
			$eku = trim($eku);
			if (array_key_exists($eku, $extended_oids)) {
				$eku = $extended_oids[$eku];
			}
		}
	} else {
		$purpose['eku'] = array();
	}
	$purpose['ca'] = (stristr($crt_details['extensions']['basicConstraints'], 'CA:TRUE') === false) ? 'No': 'Yes';
	$purpose['server'] = (in_array('TLS Web Server Authentication', $purpose['eku'])) ? 'Yes': 'No';

	return $purpose;
}

function cert_get_dates($str_crt, $decode = true) {
	if ($decode) {
		$str_crt = base64_decode($str_crt);
	}
	$crt_details = openssl_x509_parse($str_crt);
	if ($crt_details['validFrom_time_t'] > 0) {
		$start = date('r', $crt_details['validFrom_time_t']);
	}
	if ($crt_details['validTo_time_t'] > 0) {
		$end = date('r', $crt_details['validTo_time_t']);
	}
	return array($start, $end);
}

function cert_get_serial($str_crt, $decode = true) {
	if ($decode) {
		$str_crt = base64_decode($str_crt);
	}
	$crt_details = openssl_x509_parse($str_crt);
	if (isset($crt_details['serialNumber']) && !empty($crt_details['serialNumber'])) {
		return $crt_details['serialNumber'];
	} else {
		return NULL;
	}
}

function is_openvpn_server_ca($caref) {
	global $config;
	if (!is_array($config['openvpn']['openvpn-server'])) {
		return;
	}
	foreach ($config['openvpn']['openvpn-server'] as $ovpns) {
		if ($ovpns['caref'] == $caref) {
			return true;
		}
	}
	return false;
}

function is_openvpn_client_ca($caref) {
	global $config;
	if (!is_array($config['openvpn']['openvpn-client'])) {
		return;
	}
	foreach ($config['openvpn']['openvpn-client'] as $ovpnc) {
		if ($ovpnc['caref'] == $caref) {
			return true;
		}
	}
	return false;
}

function is_ipsec_peer_ca($caref) {
	global $config;
	if (!is_array($config['ipsec']['phase1'])) {
		return;
	}
	foreach ($config['ipsec']['phase1'] as $ipsec) {
		if ($ipsec['caref'] == $caref) {
			return true;
		}
	}
	return false;
}

function is_ldap_peer_ca($caref) {
	global $config;
	if (!is_array($config['system']['authserver'])) {
		return;
	}
	foreach ($config['system']['authserver'] as $authserver) {
		if ($authserver['ldap_caref'] == $caref) {
			return true;
		}
	}
	return false;
}

function ca_in_use($caref) {
	return (is_openvpn_server_ca($caref) ||
		is_openvpn_client_ca($caref) ||
		is_ipsec_peer_ca($caref) ||
		is_ldap_peer_ca($caref));
}

function is_user_cert($certref) {
	global $config;
	if (!is_array($config['system']['user'])) {
		return;
	}
	foreach ($config['system']['user'] as $user) {
		if (!is_array($user['cert'])) {
			continue;
		}
		foreach ($user['cert'] as $cert) {
			if ($certref == $cert) {
				return true;
			}
		}
	}
	return false;
}

function is_openvpn_server_cert($certref) {
	global $config;
	if (!is_array($config['openvpn']['openvpn-server'])) {
		return;
	}
	foreach ($config['openvpn']['openvpn-server'] as $ovpns) {
		if ($ovpns['certref'] == $certref) {
			return true;
		}
	}
	return false;
}

function is_openvpn_client_cert($certref) {
	global $config;
	if (!is_array($config['openvpn']['openvpn-client'])) {
		return;
	}
	foreach ($config['openvpn']['openvpn-client'] as $ovpnc) {
		if ($ovpnc['certref'] == $certref) {
			return true;
		}
	}
	return false;
}

function is_ipsec_cert($certref) {
	global $config;
	if (!is_array($config['ipsec']['phase1'])) {
		return;
	}
	foreach ($config['ipsec']['phase1'] as $ipsec) {
		if ($ipsec['certref'] == $certref) {
			return true;
		}
	}
	return false;
}

function is_webgui_cert($certref) {
	global $config;
	if (($config['system']['webgui']['ssl-certref'] == $certref) &&
	    ($config['system']['webgui']['protocol'] != "http")) {
		return true;
	}
}

function is_package_cert($certref) {
	$pluginparams = array();
	$pluginparams['type'] = 'certificates';
	$pluginparams['event'] = 'used_certificates';

	$certificates_used_by_packages = pkg_call_plugins('plugin_certificates', $pluginparams);

	/* Check if any package is using certificate */
	foreach ($certificates_used_by_packages as $name => $package) {
		if (is_array($package['certificatelist'][$certref]) &&
		    isset($package['certificatelist'][$certref]) > 0) {
			return true;
		}
	}
}

function is_captiveportal_cert($certref) {
	global $config;
	if (!is_array($config['captiveportal'])) {
		return;
	}
	foreach ($config['captiveportal'] as $portal) {
		if (isset($portal['enable']) && isset($portal['httpslogin']) && ($portal['certref'] == $certref)) {
			return true;
		}
	}
	return false;
}

function cert_in_use($certref) {

	return (is_webgui_cert($certref) ||
		is_user_cert($certref) ||
		is_openvpn_server_cert($certref) ||
		is_openvpn_client_cert($certref) ||
		is_ipsec_cert($certref) ||
		is_captiveportal_cert($certref) ||
		is_package_cert($certref));
}

function cert_usedby_description($refid, $certificates_used_by_packages) {
	$result = "";
	if (is_array($certificates_used_by_packages)) {
		foreach ($certificates_used_by_packages as $name => $package) {
			if (isset($package['certificatelist'][$refid])) {
				$hint = "" ;
				if (is_array($package['certificatelist'][$refid])) {
					foreach ($package['certificatelist'][$refid] as $cert_used) {
						$hint = $hint . $cert_used['usedby']."\n";
					}
				}
				$count = count($package['certificatelist'][$refid]);
				$result .= "<div title='".htmlspecialchars($hint)."'>";
				$result .= htmlspecialchars($package['pkgname'])." ($count)<br />";
				$result .= "</div>";
			}
		}
	}
	return $result;
}

function crl_create(& $crl, $caref, $name, $serial = 0, $lifetime = 9999) {
	global $config;
	$ca =& lookup_ca($caref);
	if (!$ca) {
		return false;
	}
	$crl['descr'] = $name;
	$crl['caref'] = $caref;
	$crl['serial'] = $serial;
	$crl['lifetime'] = $lifetime;
	$crl['cert'] = array();
	$crl_res = crl_update($crl);
	$config['crl'][] = $crl;
	return $crl_res;
}

function crl_update(& $crl) {
	global $config;
	$ca =& lookup_ca($crl['caref']);
	if (!$ca) {
		return false;
	}
	// If we have text but no certs, it was imported and cannot be updated.
	if (($crl["method"] != "internal") && (!empty($crl['text']) && empty($crl['cert']))) {
		return false;
	}
	$crl['serial']++;
	$ca_str_crt = base64_decode($ca['crt']);
	$ca_str_key = base64_decode($ca['prv']);
	$crl_res = openssl_crl_new($ca_str_crt, $crl['serial'], $crl['lifetime']);
	if (is_array($crl['cert']) && (count($crl['cert']) > 0)) {
		foreach ($crl['cert'] as $cert) {
			openssl_crl_revoke_cert($crl_res, base64_decode($cert["crt"]), $cert["revoke_time"], $cert["reason"]);
		}
	}
	openssl_crl_export($crl_res, $crl_text, $ca_str_key);
	$crl['text'] = base64_encode($crl_text);
	return $crl_res;
}

function cert_revoke($cert, & $crl, $reason = OCSP_REVOKED_STATUS_UNSPECIFIED) {
	global $config;
	if (is_cert_revoked($cert, $crl['refid'])) {
		return true;
	}
	// If we have text but no certs, it was imported and cannot be updated.
	if (!is_crl_internal($crl)) {
		return false;
	}
	$cert["reason"] = $reason;
	$cert["revoke_time"] = time();
	$crl["cert"][] = $cert;
	crl_update($crl);
	return true;
}

function cert_unrevoke($cert, & $crl) {
	global $config;
	if (!is_crl_internal($crl)) {
		return false;
	}
	foreach ($crl['cert'] as $id => $rcert) {
		if (($rcert['refid'] == $cert['refid']) || ($rcert['descr'] == $cert['descr'])) {
			unset($crl['cert'][$id]);
			if (count($crl['cert']) == 0) {
				// Protect against accidentally switching the type to imported, for older CRLs
				if (!isset($crl['method'])) {
					$crl['method'] = "internal";
				}
				crl_update($crl);
			} else {
				crl_update($crl);
			}
			return true;
		}
	}
	return false;
}

/* Compare two certificates to see if they match. */
function cert_compare($cert1, $cert2) {
	/* Ensure two certs are identical by first checking that their issuers match, then
		subjects, then serial numbers, and finally the moduli. Anything less strict
		could accidentally count two similar, but different, certificates as
		being identical. */
	$c1 = base64_decode($cert1['crt']);
	$c2 = base64_decode($cert2['crt']);
	if ((cert_get_issuer($c1, false) == cert_get_issuer($c2, false)) &&
	    (cert_get_subject($c1, false) == cert_get_subject($c2, false)) &&
	    (cert_get_serial($c1, false) == cert_get_serial($c2, false)) &&
	    (cert_get_publickey($c1, false) == cert_get_publickey($c2, false))) {
		return true;
	}
	return false;
}

function is_cert_revoked($cert, $crlref = "") {
	global $config;
	if (!is_array($config['crl'])) {
		return false;
	}

	if (!empty($crlref)) {
		$crl = lookup_crl($crlref);
		if (!is_array($crl['cert'])) {
			return false;
		}
		foreach ($crl['cert'] as $rcert) {
			if (cert_compare($rcert, $cert)) {
				return true;
			}
		}
	} else {
		foreach ($config['crl'] as $crl) {
			if (!is_array($crl['cert'])) {
				continue;
			}
			foreach ($crl['cert'] as $rcert) {
				if (cert_compare($rcert, $cert)) {
					return true;
				}
			}
		}
	}
	return false;
}

function is_openvpn_server_crl($crlref) {
	global $config;
	if (!is_array($config['openvpn']['openvpn-server'])) {
		return;
	}
	foreach ($config['openvpn']['openvpn-server'] as $ovpns) {
		if (!empty($ovpns['crlref']) && ($ovpns['crlref'] == $crlref)) {
			return true;
		}
	}
	return false;
}

// Keep this general to allow for future expansion. See cert_in_use() above.
function crl_in_use($crlref) {
	return (is_openvpn_server_crl($crlref));
}

function is_crl_internal($crl) {
	return (!(!empty($crl['text']) && empty($crl['cert'])) || ($crl["method"] == "internal"));
}

function cert_get_cn($crt, $isref = false) {
	/* If this is a certref, not an actual cert, look up the cert first */
	if ($isref) {
		$cert = lookup_cert($crt);
		/* If it's not a valid cert, bail. */
		if (!(is_array($cert) && !empty($cert['crt']))) {
			return "";
		}
		$cert = $cert['crt'];
	} else {
		$cert = $crt;
	}
	$sub = cert_get_subject_array($cert);
	if (is_array($sub)) {
		foreach ($sub as $s) {
			if (strtoupper($s['a']) == "CN") {
				return $s['v'];
			}
		}
	}
	return "";
}

function cert_escape_x509_chars($str, $reverse = false) {
	/* Characters which need escaped when present in x.509 fields.
	 * See https://www.ietf.org/rfc/rfc4514.txt
	 *
	 * The backslash (\) must be listed first in these arrays!
	 */
	$cert_directory_string_special_chars = array('\\', '"', '#', '+', ',', ';', '<', '=', '>');
	$cert_directory_string_special_chars_esc = array('\\\\', '\"', '\#', '\+', '\,', '\;', '\<', '\=', '\>');
	if ($reverse) {
		return str_replace($cert_directory_string_special_chars_esc, $cert_directory_string_special_chars, $str);
	} else {
		/* First unescape and then escape again, to prevent possible double escaping. */
		return str_replace($cert_directory_string_special_chars, $cert_directory_string_special_chars_esc, cert_escape_x509_chars($str, true));
	}
}

function cert_add_altname_type($str) {
	$type = "";
	if (is_ipaddr($str)) {
		$type = "IP";
	} elseif (is_hostname($str)) {
		$type = "DNS";
	} elseif (is_URL($str)) {
		$type = "URI";
	} elseif (filter_var($str, FILTER_VALIDATE_EMAIL)) {
		$type = "email";
	}
	if (!empty($type)) {
		return "{$type}:" . cert_escape_x509_chars($str);
	} else {
		return "";
	}
}

function cert_type_config_section($type) {
	switch ($type) {
		case "ca":
			$cert_type = "v3_ca";
			break;
		case "server":
		case "self-signed":
			$cert_type = "server";
			break;
		default:
			$cert_type = "usr_cert";
			break;
	}
	return $cert_type;
}

?>
