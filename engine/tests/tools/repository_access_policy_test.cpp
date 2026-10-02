#include "amoeba/tools/repository_access_policy.hpp"

#include <gtest/gtest.h>
#include <filesystem>

using namespace amoeba::tools;

class RepositoryAccessPolicyTest : public ::testing::Test {
protected:
    std::filesystem::path repo_root{"/fake/repo"};
    RepositoryAccessPolicy policy;
};

TEST_F(RepositoryAccessPolicyTest, AllowValidRelativeSourceFiles) {
    EXPECT_EQ(policy.check_read(repo_root, "src/main.cpp"), AccessDecision::Allowed);
    EXPECT_EQ(policy.check_read(repo_root, "src/auth/auth_service.cpp"), AccessDecision::Allowed);
    EXPECT_EQ(policy.check_read(repo_root, "include/amoeba/tokens/jwt_token.hpp"), AccessDecision::Allowed);
    EXPECT_EQ(policy.check_read(repo_root, "components/calendar/Calendar.tsx"), AccessDecision::Allowed);
    EXPECT_EQ(policy.check_read(repo_root, "config/database_config.cpp"), AccessDecision::Allowed);
}

TEST_F(RepositoryAccessPolicyTest, RejectEmptyPath) {
    EXPECT_EQ(policy.check_read(repo_root, ""), AccessDecision::InvalidPath);
}

TEST_F(RepositoryAccessPolicyTest, RejectAbsoluteWindowsPaths) {
    EXPECT_EQ(policy.check_read(repo_root, "C:/Users/Admin/secret.txt"), AccessDecision::PathOutsideRepository);
    EXPECT_EQ(policy.check_read(repo_root, "C:\\Windows\\System32\\cmd.exe"), AccessDecision::PathOutsideRepository);
    EXPECT_EQ(policy.check_read(repo_root, "D:/amoeba/secret.txt"), AccessDecision::PathOutsideRepository);
    EXPECT_EQ(policy.check_read(repo_root, "d:\\amoeba\\secret.txt"), AccessDecision::PathOutsideRepository);
}

TEST_F(RepositoryAccessPolicyTest, RejectAbsoluteUnixPaths) {
    EXPECT_EQ(policy.check_read(repo_root, "/etc/passwd"), AccessDecision::PathOutsideRepository);
    EXPECT_EQ(policy.check_read(repo_root, "/var/log/syslog"), AccessDecision::PathOutsideRepository);
    EXPECT_EQ(policy.check_read(repo_root, "/home/user/.ssh/id_rsa"), AccessDecision::PathOutsideRepository);
}

TEST_F(RepositoryAccessPolicyTest, RejectParentTraversalSlash) {
    EXPECT_EQ(policy.check_read(repo_root, "../secret.txt"), AccessDecision::PathOutsideRepository);
    EXPECT_EQ(policy.check_read(repo_root, "../../secret.txt"), AccessDecision::PathOutsideRepository);
    EXPECT_EQ(policy.check_read(repo_root, "src/../../secret.txt"), AccessDecision::PathOutsideRepository);
    EXPECT_EQ(policy.check_read(repo_root, "src/utils/../../../etc/passwd"), AccessDecision::PathOutsideRepository);
}

TEST_F(RepositoryAccessPolicyTest, RejectParentTraversalBackslash) {
    EXPECT_EQ(policy.check_read(repo_root, "..\\secret.txt"), AccessDecision::PathOutsideRepository);
    EXPECT_EQ(policy.check_read(repo_root, "..\\..\\secret.txt"), AccessDecision::PathOutsideRepository);
    EXPECT_EQ(policy.check_read(repo_root, "src\\..\\..\\secret.txt"), AccessDecision::PathOutsideRepository);
}

TEST_F(RepositoryAccessPolicyTest, RejectSensitiveEnvFiles) {
    EXPECT_EQ(policy.check_read(repo_root, ".env"), AccessDecision::SensitiveFile);
    EXPECT_EQ(policy.check_read(repo_root, ".env.local"), AccessDecision::SensitiveFile);
    EXPECT_EQ(policy.check_read(repo_root, ".env.production"), AccessDecision::SensitiveFile);
    EXPECT_EQ(policy.check_read(repo_root, "config/.env"), AccessDecision::SensitiveFile);
    EXPECT_EQ(policy.check_read(repo_root, "src/.env.development"), AccessDecision::SensitiveFile);
}

TEST_F(RepositoryAccessPolicyTest, RejectKeyAndCertFiles) {
    EXPECT_EQ(policy.check_read(repo_root, "server.key"), AccessDecision::SensitiveFile);
    EXPECT_EQ(policy.check_read(repo_root, "certs/privkey.pem"), AccessDecision::SensitiveFile);
    EXPECT_EQ(policy.check_read(repo_root, "ssl/cert.p12"), AccessDecision::SensitiveFile);
    EXPECT_EQ(policy.check_read(repo_root, "client.pfx"), AccessDecision::SensitiveFile);
    EXPECT_EQ(policy.check_read(repo_root, "keystore.jks"), AccessDecision::SensitiveFile);
    EXPECT_EQ(policy.check_read(repo_root, "secrets.kdbx"), AccessDecision::SensitiveFile);
}

TEST_F(RepositoryAccessPolicyTest, RejectSSHAndGitDirectories) {
    EXPECT_EQ(policy.check_read(repo_root, ".ssh/id_rsa"), AccessDecision::SensitiveFile);
    EXPECT_EQ(policy.check_read(repo_root, ".ssh/id_ed25519"), AccessDecision::SensitiveFile);
    EXPECT_EQ(policy.check_read(repo_root, "id_rsa"), AccessDecision::SensitiveFile);
    EXPECT_EQ(policy.check_read(repo_root, "id_ed25519"), AccessDecision::SensitiveFile);
    EXPECT_EQ(policy.check_read(repo_root, ".git/config"), AccessDecision::SensitiveFile);
    EXPECT_EQ(policy.check_read(repo_root, ".git/HEAD"), AccessDecision::SensitiveFile);
}

TEST_F(RepositoryAccessPolicyTest, RejectSecretDirectories) {
    EXPECT_EQ(policy.check_read(repo_root, "secret/key.txt"), AccessDecision::SensitiveFile);
    EXPECT_EQ(policy.check_read(repo_root, "secrets/passwords.json"), AccessDecision::SensitiveFile);
    EXPECT_EQ(policy.check_read(repo_root, "credentials/aws.json"), AccessDecision::SensitiveFile);
    EXPECT_EQ(policy.check_read(repo_root, "submodule/secrets/token.txt"), AccessDecision::SensitiveFile);
}

TEST_F(RepositoryAccessPolicyTest, CustomPatternRegistration) {
    RepositoryAccessPolicy custom_policy;
    custom_policy.add_sensitive_pattern("internal_api_key.json");
    EXPECT_EQ(custom_policy.check_read(repo_root, "internal_api_key.json"), AccessDecision::SensitiveFile);
}
