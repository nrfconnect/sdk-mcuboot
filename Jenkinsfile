//
// Copyright (c) 2020 Nordic Semiconductor ASA. All Rights Reserved.
//
// The information contained herein is confidential property of Nordic Semiconductor ASA.
// The use, copying, transfer or disclosure of such information is prohibited except by
// express written agreement with Nordic Semiconductor ASA.
//

@Library("CI_LIB") _

HashMap CI_STATE = lib_State.getConfig(JOB_NAME)
properties(lib_State.getTriggers())

pipeline {

  parameters {
   booleanParam(name: 'RUN_DOWNSTREAM', description: 'if false skip downstream jobs', defaultValue: false)
   booleanParam(name: 'RUN_TESTS', description: 'if false skip testing', defaultValue: true)
   booleanParam(name: 'RUN_BUILD', description: 'if false skip building', defaultValue: true)
   string(      name: 'jsonstr_CI_STATE', description: 'Default State if no upstream job', defaultValue: CI_STATE.CFG.INPUT_STATE_STR )
   choice(name: 'TEST_CYCLE', choices: CI_STATE.CFG.CRON_CHOICES, description: 'Cron Test Phase')
  }

  agent {
    docker {
      image CI_STATE.CFG.IMAGE_TAG
      label CI_STATE.CFG.AGENT_LABELS
    }
  }

  options {
    checkoutToSubdirectory('bootloader/mcuboot')
    parallelsAlwaysFailFast()
    timeout(time: CI_STATE.CFG.TIMEOUT.time, unit: CI_STATE.CFG.TIMEOUT.unit)
  }

  environment {
      // This token is used to by check_compliance to comment on PRs and use checks
      GH_TOKEN = credentials('nordicbuilder-compliance-token')
      GH_USERNAME = "NordicBuilder"
  }

  stages {
    stage('Load') { steps { script { CI_STATE = lib_State.load('MCUBOOT', CI_STATE) }}}
    stage('Checkout') {
      steps { script {
        println "Running on NODE: $NODE_NAME"
        lib_Main.checkoutRepo(CI_STATE.NRF.GIT_URL, "nrf", CI_STATE.NRF, true)
        lib_West.InitUpdate('nrf')
        dir('bootloader') {
          CI_STATE.SELF.REPORT_SHA = lib_Main.checkoutRepo(CI_STATE.SELF.GIT_URL, "mcuboot", CI_STATE.SELF, false)
          dir('mcuboot') {
            sh "git log --graph --oneline --decorate -n 10"
          }
        }
      }}
    }
    stage('Run compliance check') {
      when { expression { CI_STATE.SELF.RUN_TESTS } }
      steps {
        script {
          println "Running on NODE: $NODE_NAME"
          lib_Status.set("PENDING", 'MCUBOOT', CI_STATE);
          dir('bootloader/mcuboot') {

            CI_STATE.SELF.ORG_AND_REPO = CI_STATE.SELF.GIT_URL.replace('.git','').replace('https://github.com/','')
            CI_STATE.SELF.COMPLIANCE = new HashMap()
            CI_STATE.SELF.COMPLIANCE.ARGS  = " -r $CI_STATE.SELF.ORG_AND_REPO "

            def BUILD_TYPE = lib_Main.getBuildType(CI_STATE.SELF)
            if (BUILD_TYPE == "PR") {

              if ( CI_STATE.SELF.CHANGE_TITLE.toLowerCase().contains('[nrf mergeup]') )  {
                CI_STATE.SELF.IS_MERGEUP = true
                println 'This is a MERGE-UP PR.   CI_STATE.SELF.IS_MERGEUP=' + CI_STATE.SELF.IS_MERGEUP
                CI_STATE.SELF.MERGEUP_BASE = sh( script: "git log --oneline --grep='\\[nrf mergeup\\].*' -i -n 1 --pretty=format:'%h' | tr -d '\\n'" , returnStdout: true)
                println "CI_STATE.SELF.MERGEUP_BASE = $CI_STATE.SELF.MERGEUP_BASE"
                COMMIT_RANGE = "$CI_STATE.SELF.MERGEUP_BASE..$CI_STATE.SELF.REPORT_SHA"
              } else {
                CI_STATE.SELF.IS_MERGEUP = false
                COMMIT_RANGE = "$CI_STATE.SELF.MERGE_BASE..$CI_STATE.SELF.REPORT_SHA"
              }

              CI_STATE.SELF.COMPLIANCE.ARGS += " -p $CHANGE_ID "
              println "Building a PR [$CHANGE_ID]: $COMMIT_RANGE"
            }
            else if (BUILD_TYPE == "TAG") {
              COMMIT_RANGE = "tags/${env.BRANCH_NAME}..tags/${env.BRANCH_NAME}"
              println "Building a Tag: " + COMMIT_RANGE
            }
            // If not a PR, it's a non-PR-branch or master build. Compare against the origin.
            else if (BUILD_TYPE == "BRANCH") {
              COMMIT_RANGE = "origin/${env.BRANCH_NAME}..HEAD"
              println "Building a Branch: " + COMMIT_RANGE
            }
            else {
                assert condition : "Build fails because it is not a PR/Tag/Branch"
            }

            CI_STATE.SELF.COMPLIANCE.ARGS += " -S $CI_STATE.SELF.REPORT_SHA "
            CI_STATE.SELF.COMPLIANCE.ARGS += " --commits $COMMIT_RANGE "

            // Run the compliance check
            try {
              sh """
                echo $CI_STATE.SELF.COMPLIANCE.ARGS
                source ../../zephyr/zephyr-env.sh
                ../../zephyr/scripts/ci/check_compliance.py $CI_STATE.SELF.COMPLIANCE.ARGS
              """
            }
            finally {
              junit 'compliance.xml'
              archiveArtifacts artifacts: 'compliance.xml'
            }
          }
        }
      }
    }

    stage('Build samples') {
      when { expression { CI_STATE.SELF.RUN_BUILD } }
      steps {
          echo "No Samples to build yet."
      }
    }

    stage('Trigger Downstream Jobs') {
      when { expression { CI_STATE.SELF.RUN_DOWNSTREAM } }
      steps { script { lib_Stage.runDownstream(JOB_NAME, CI_STATE) } }
    }

    stage('Report') {
      when { expression { CI_STATE.SELF.RUN_TESTS } }
      steps { script {
          println 'no report generation yet'
      } }
    }

  }

  post {
    // This is the order that the methods are run. {always->success/abort/failure/unstable->cleanup}
    always { script {
      lib_Status.set( "${currentBuild.currentResult}", 'MCUBOOT', CI_STATE)
      if ( !CI_STATE.SELF.RUN_BUILD || !CI_STATE.SELF.RUN_TESTS ) { currentBuild.result = "UNSTABLE"}
    }}
    // Add if needed
    // success {}
    // aborted {}
    // unstable {}
    failure {
      echo "failure"
      script{
        if ( env.JOB_NAME == 'latest/sdk-mcuboot/master' )
        {
            emailext(to: 'anpu',
                body: "${currentBuild.currentResult}\nJob ${env.JOB_NAME}\t\t build ${env.BUILD_NUMBER}\r\nLink: ${env.BUILD_URL}",
                subject: "[Jenkins][Build ${currentBuild.currentResult}: ${env.JOB_NAME}]",
                mimeType: 'text/html',)
        }
        else
        {
            echo "Branch ${env.BRANCH_NAME} is not master nor PR. Sending failure email skipped."
        }
      }
    }
    cleanup {
        echo "Pipeline Post: cleanup"
        cleanWs disableDeferredWipeout: true, deleteDirs: true
    }
  }
}
