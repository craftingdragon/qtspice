# Security reporting

Do not put exploit details, credentials, or private connection files in public issues.
Use GitHub's **Security → Report a vulnerability** on the project repository when
private reporting is available. If unavailable, open an issue requesting a private
contact channel, without disclosing the vulnerability. The maintainer must enable
private vulnerability reporting in repository settings; this checkout cannot enable it.

Include the affected commit, dependency versions, impact, and a minimal reproduction
with synthetic data. Redact passwords, session tickets, proxy credentials, clipboard
contents, and private endpoints. There is no guaranteed response time or supported
stable release branch yet; fixes target the current development branch.
