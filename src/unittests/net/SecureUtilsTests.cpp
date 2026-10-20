/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2021 Barrier Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "SecureUtilsTests.h"

#include "net/SecureUtils.h"
#include "common/Constants.h"

#include <QFile>
#include <QSslCertificate>
#include <QSslKey>
#include <QTemporaryDir>

#include <memory>
#include <openssl/pem.h>

using namespace deskflow;

void SecureUtilsTests::checkHex()
{
  const QByteArray fingerprint(
      "\x28\xFD\x0A\x98\x8A\x0E\xA1\x6C\xD7\xE8\x6C\xA7\xEE\x58\x41\x71\xCA\xB2\x8E\x49\x25\x94\x90\x25\x26\x05\x8D\xAF"
      "\x63\xED\x2E\x30",
      32
  );
  QCOMPARE(
      deskflow::formatSSLFingerprint(fingerprint, true),
      "28:FD:0A:98:8A:0E:A1:6C:D7:E8:6C:A7:EE:58:41:71:CA:B2:8E:49:25:94:90:25:26:05:8D:AF:63:ED:2E:30"
  );
}

void SecureUtilsTests::checkArt()
{
  const QByteArray fingerprint(
      "\x28\xFD\x0A\x98\x8A\x0E\xA1\x6C\xD7\xE8\x6C\xA7\xEE\x58\x41\x71\xCA\xB2\x8E\x49\x25\x94\x90\x25\x26\x05\x8D\xAF"
      "\x63\xED\x2E\x30",
      32
  );
  QCOMPARE(
      deskflow::generateFingerprintArt(fingerprint), "╔═════════════════╗\n"
                                                     "║*X+. .           ║\n"
                                                     "║*oo +            ║\n"
                                                     "║ + =             ║\n"
                                                     "║  B  . .         ║\n"
                                                     "║.+... o S        ║\n"
                                                     "║E+ ++. .         ║\n"
                                                     "║B*++..  .        ║\n"
                                                     "║+o*o o .         ║\n"
                                                     "║+o*Bo .          ║\n"
                                                     "╚═════════════════╝"
  );
}

void SecureUtilsTests::generatedCertificateUsesApplicationIdentity()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto path = directory.filePath(QStringLiteral("identity.pem"));
  generatePemSelfSignedCert(path);

  QFile file(path);
  QVERIFY(file.open(QIODevice::ReadOnly));
  const auto pem = file.readAll();
  const auto certificates = QSslCertificate::fromData(pem);
  QCOMPARE(certificates.size(), 1);
  const auto certificate = certificates.first();
  QCOMPARE(certificate.subjectInfo(QSslCertificate::CommonName), QStringList{QString::fromUtf8(kAppName)});
  QCOMPARE(certificate.issuerInfo(QSslCertificate::CommonName), QStringList{QString::fromUtf8(kAppName)});
  QCOMPARE(certificate.publicKey().length(), 2048);

  // Verify the signature against the generated private key, so the application
  // name is part of a usable self-signed identity rather than a display label.
  const auto buffer = std::unique_ptr<BIO, decltype(&BIO_free)>(BIO_new_mem_buf(pem.constData(), pem.size()), BIO_free);
  QVERIFY(buffer);
  const auto key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>(
      PEM_read_bio_PrivateKey(buffer.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
  QVERIFY(key);
  QVERIFY(BIO_reset(buffer.get()) > 0);
  const auto signedCertificate = std::unique_ptr<X509, decltype(&X509_free)>(
      PEM_read_bio_X509(buffer.get(), nullptr, nullptr, nullptr), X509_free);
  QVERIFY(signedCertificate);
  QCOMPARE(X509_check_private_key(signedCertificate.get(), key.get()), 1);
  QCOMPARE(X509_verify(signedCertificate.get(), key.get()), 1);
}

void SecureUtilsTests::independentlyGeneratedCertificatesHaveDifferentFingerprints()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto first = directory.filePath(QStringLiteral("first.pem"));
  const auto second = directory.filePath(QStringLiteral("second.pem"));
  generatePemSelfSignedCert(first);
  generatePemSelfSignedCert(second);
  const auto firstCertificates = QSslCertificate::fromPath(first);
  const auto secondCertificates = QSslCertificate::fromPath(second);
  QCOMPARE(firstCertificates.size(), 1);
  QCOMPARE(secondCertificates.size(), 1);
  QVERIFY(firstCertificates.first().digest(QCryptographicHash::Sha256) !=
          secondCertificates.first().digest(QCryptographicHash::Sha256));
}

QTEST_MAIN(SecureUtilsTests)
