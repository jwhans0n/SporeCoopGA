# Saved dependency receipt fixture

The three binary artifacts are exact copies from the closed private C34 test:
the genuine 60,745-byte Native Saved image, its four-record immutable Vehicle
package, and the independently tested OUT prototype receipt. Their source paths,
sizes and SHA-256 values are in `manifest.json`; repository tests verify all
three before using them. No personal profile or live database was modified.

The test independently decodes the actual Saved image and checks all 119 class
IDs, all three references per class, the Captain and the actual empty Crew:
358 raw reference slots. Raw key, machine, server and model are checked against
the retained receipt, including unused class slots. The four actual package
records match the receipt byte-for-byte. The 193-byte metadata is retained.

Stock database tags and the test profile hash are controlled issuer values.
This fixture did not obtain genuine Native stock ownership or selection
authority. A production issuer must independently observe those under its
Native identity/resolver/lifetime guards. Missing custom bytes never prove stock.

The codec must reproduce the complete old prototype receipt bytes. Six-family
positives cover the existing Collector's fixed BEM/metadata/image companion
contract; they are controlled value cases, not six running-game admissions.
Unknown families and foreign/private companion relations reject. Arbitrary
recursive BEM/private-part dependency completeness is not claimed.

The filesystem fixture publishes only under its explicitly supplied test parent.
Each run reserves a fresh directory; old test results are retained. It covers
equal and unequal concurrent no-replace publication, every private fault stage,
durability retry, corruption, path rejection and previous-receipt preservation.
Fault injection is confined to the store test translation unit and is absent
from the public API and production runtime controls.
