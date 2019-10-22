
@Library("CI_LIB") _

def AGENT_LABELS = lib_Main.getAgentLabels(JOB_NAME)
def IMAGE_TAG    = lib_Main.getDockerImage(JOB_NAME)
def TIMEOUT      = lib_Main.getTimeout(JOB_NAME)
def INPUT_STATE  = lib_Main.getInputState(JOB_NAME)
def CI_STATE = new HashMap()

pipeline {

  parameters {
   booleanParam(name: 'RUN_DOWNSTREAM', description: 'if false skip downstream jobs', defaultValue: false)
   booleanParam(name: 'RUN_TESTS', description: 'if false skip testing', defaultValue: true)
   booleanParam(name: 'RUN_BUILD', description: 'if false skip building', defaultValue: true)
   string(name: 'jsonstr_CI_STATE', description: 'Default State if no upstream job', defaultValue: INPUT_STATE)
  }

  triggers {
    cron(env.BRANCH_NAME == 'master' ? '0 */12 * * 1-6' : '') // Master branch will be build every 12 hours
  }

  agent {
    docker {
      image IMAGE_TAG
      label AGENT_LABELS
    }
  }

  options {
    // Checkout the repository to this folder instead of root
    checkoutToSubdirectory('mcuboot')
    timeout(time: TIMEOUT.time, unit: TIMEOUT.unit)
  }

  environment {
      // This token is used to by check_compliance to comment on PRs and use checks
      GH_TOKEN = credentials('nordicbuilder-compliance-token')
      GH_USERNAME = "NordicBuilder"
      COMPLIANCE_ARGS = "-r NordicPlayground/fw-nrfconnect-mcuboot"
  }

  stages {
    stage('Load') { steps { script { CI_STATE = lib_Stage.load('MCUBOOT') }}}
    stage('Checkout') {
      steps { script {
        lib_Main.cloneCItools(JOB_NAME)
        CI_STATE.MCUBOOT.REPORT_SHA = lib_Main.checkoutRepo(CI_STATE.MCUBOOT.GIT_URL, "mcuboot", CI_STATE.MCUBOOT, false)
        lib_West.AddManifestUpdate("MCUBOOT", 'mcuboot', CI_STATE.MCUBOOT.GIT_URL, CI_STATE.MCUBOOT.GIT_REF, CI_STATE)
      }}
    }
    stage('Run compliance check') {
      when { expression { CI_STATE.MCUBOOT.RUN_TESTS } }
      steps {
        script {
          lib_Status.set("PENDING", 'MCUBOOT', CI_STATE);
          dir('mcuboot') {

            def BUILD_TYPE = lib_Main.getBuildType(CI_STATE.MCUBOOT)
            if (BUILD_TYPE == "PR") {

              if (CI_STATE.MCUBOOT.CHANGE_TITLE.toLowerCase().contains('[nrf mergeup]')) {
                COMPLIANCE_ARGS = "$COMPLIANCE_ARGS --exclude-module Gitlint"
              }

              COMMIT_RANGE = "$CI_STATE.MCUBOOT.MERGE_BASE..$CI_STATE.MCUBOOT.REPORT_SHA"
              COMPLIANCE_ARGS = "$COMPLIANCE_ARGS -p $CHANGE_ID -S $CI_STATE.MCUBOOT.REPORT_SHA -g -e pylint"
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

            // Run the compliance check
            try {
              sh "../ci-tools/scripts/check_compliance.py $COMPLIANCE_ARGS --commits $COMMIT_RANGE"
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
      when { expression { CI_STATE.MCUBOOT.RUN_BUILD } }
      steps {
          echo "No Samples to build yet."
      }
    }
    stage('Trigger testing build') {
      when { expression { CI_STATE.MCUBOOT.RUN_DOWNSTREAM } }
      steps {
        script {
          CI_STATE.MCUBOOT.WAITING = true
          def DOWNSTREAM_JOBS = lib_Main.getDownStreamJobs(JOB_NAME)
          def jobs = [:]
          DOWNSTREAM_JOBS.each {
            jobs["${it}"] = {
              build job: "${it}", propagate: true, wait: true, parameters: [
                        string(name: 'jsonstr_CI_STATE', value: lib_Util.HashMap2Str(CI_STATE))]
            }
          }
          parallel jobs
        }
      }
    }

  }

  post {
    // This is the order that the methods are run. {always->success/abort/failure/unstable->cleanup}
    always {
      echo "always"
    }
    success {
      echo "success"
      script { lib_Status.set("SUCCESS", 'MCUBOOT', CI_STATE) }
    }
    aborted {
      echo "aborted"
      script { lib_Status.set("ABORTED", 'MCUBOOT', CI_STATE) }
    }
    unstable {
      echo "unstable"
    }
    failure {
      echo "failure"
      script { lib_Status.set("FAILURE", 'MCUBOOT', CI_STATE) }
      script{
        if (env.BRANCH_NAME == 'master' || env.BRANCH_NAME.startsWith("PR"))
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
        echo "cleanup"
        // Clean up the working space at the end (including tracked files)
        cleanWs()
    }
  }
}
