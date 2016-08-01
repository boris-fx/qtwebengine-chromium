// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SIGNIN_CORE_BROWSER_ACCOUNT_INVESTIGATOR_H_
#define COMPONENTS_SIGNIN_CORE_BROWSER_ACCOUNT_INVESTIGATOR_H_

#include <string>
#include <vector>

#include "base/macros.h"
#include "base/timer/timer.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/signin/core/browser/gaia_cookie_manager_service.h"

struct AccountInfo;
class GaiaCookieManagerService;
class PrefRegistrySimple;
class PrefService;
class SigninManagerBase;

namespace base {
class Time;
}  // namespace base

namespace signin_metrics {
enum class AccountRelation;
enum class ReportingType;
}  // namespace signin_metrics

// While it is common for the account signed into Chrome and the content area
// to be identical, this is not the only scenario. The purpose of this class
// is to watch for changes in relation between Chrome and content area accounts
// and emit metrics about their relation.
class AccountInvestigator : public KeyedService,
                            public GaiaCookieManagerService::Observer {
 public:
  // The targeted interval to perform periodic reporting. If chrome is not
  // active at the end of an interval, reporting will be done as soon as
  // possible.
  static const base::TimeDelta kPeriodicReportingInterval;

  AccountInvestigator(GaiaCookieManagerService* cookie_service,
                      PrefService* pref_service,
                      SigninManagerBase* signin_manager);
  ~AccountInvestigator() override;

  static void RegisterPrefs(PrefRegistrySimple* registry);

  // Sets up initial state and starts the timer to perform periodic reporting.
  void Initialize();

  // KeyedService:
  void Shutdown() override;

  // GaiaCookieManagerService::Observer:
  void OnAddAccountToCookieCompleted(
      const std::string& account_id,
      const GoogleServiceAuthError& error) override;
  void OnGaiaAccountsInCookieUpdated(
      const std::vector<gaia::ListedAccount>& signed_in_accounts,
      const std::vector<gaia::ListedAccount>& signed_out_accounts,
      const GoogleServiceAuthError& error) override;

 private:
  friend class AccountInvestigatorTest;

  // Calculates the amount of time to wait/delay before the given |interval| has
  // passed since |previous|, given |now|.
  static base::TimeDelta CalculatePeriodicDelay(base::Time previous,
                                                base::Time now,
                                                base::TimeDelta interval);

  // Calculate a hash of the listed accounts. Order of accounts should not
  // affect the hashed values, but signed in and out status should.
  static std::string HashAccounts(
      const std::vector<gaia::ListedAccount>& signed_in_accounts,
      const std::vector<gaia::ListedAccount>& signed_out_accounts);

  // Compares the |info| object, which should correspond to the currently or
  // potentially signed into Chrome account, to the various account(s) in the
  // given cookie jar.
  static signin_metrics::AccountRelation DiscernRelation(
      const AccountInfo& info,
      const std::vector<gaia::ListedAccount>& signed_in_accounts,
      const std::vector<gaia::ListedAccount>& signed_out_accounts);

  // Tries to perform periodic reporting, potentially performing it now if the
  // cookie information is cached, otherwise sets things up to perform this
  // reporting asynchronously.
  void TryPeriodicReport();

  // Performs periodic reporting with the given cookie jar data and restarts
  // the periodic reporting timer.
  void DoPeriodicReport(
      const std::vector<gaia::ListedAccount>& signed_in_accounts,
      const std::vector<gaia::ListedAccount>& signed_out_accounts);

  // Performs the reporting that's shared between the periodic case and the
  // on change case.
  void SharedCookieJarReport(
      const std::vector<gaia::ListedAccount>& signed_in_accounts,
      const std::vector<gaia::ListedAccount>& signed_out_accounts,
      const base::Time now,
      const signin_metrics::ReportingType type);

  // Performs only the account relation reporting, which means comparing the
  // signed in account to the cookie jar accounts(s).
  void SignedInAccountRelationReport(
      const std::vector<gaia::ListedAccount>& signed_in_accounts,
      const std::vector<gaia::ListedAccount>& signed_out_accounts,
      signin_metrics::ReportingType type);

  GaiaCookieManagerService* cookie_service_;
  PrefService* pref_service_;
  SigninManagerBase* signin_manager_;

  // Handles invoking our periodic logic at the right time. As part of our
  // handling of this call we reset the timer for the next loop.
  base::OneShotTimer timer_;

  // If the GaiaCookieManagerService hasn't already cached the cookie data, it
  // will not be able to return enough information for us to always perform
  // periodic reporting synchronously. This flag is flipped to true when we're
  // waiting for this, and will allow us to perform periodic reporting when
  // we're called as an observer on a potential cookie jar data change. The
  // typical time this is occurs occurs during startup.
  bool periodic_pending_ = false;

  // Gets set upon initialization and on potential cookie jar change. This
  // allows us ot emit AccountRelation metrics during a sign in that doesn't
  // actually change the cookie jar.
  bool previously_authenticated_ = false;

  DISALLOW_COPY_AND_ASSIGN(AccountInvestigator);
};

#endif  // COMPONENTS_SIGNIN_CORE_BROWSER_ACCOUNT_INVESTIGATOR_H_
